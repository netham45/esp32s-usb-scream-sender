
#include <stdio.h>
#include <math.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "tusb.h"
#include "usb_device_uac.h"

#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "api.h"
#include "secrets.h"

const char header[] = {1, 16, 2, 0, 0}; // Stereo 16 bit 48KHz default layout

#define HEADER_SIZE sizeof(header)

#define CHUNK_SIZE 1152

#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE)

static const char *TAG = "ESP32SUSBScreamSender";

bool is_muted = false;
uint32_t volume = 100;
uint32_t new_volume = 1;


char data_out[PACKET_SIZE];

char data_in[CHUNK_SIZE * 10];

int data_in_head = 0;

bool connected = false;

int sock = -1;
struct sockaddr_in dest_addr;

int reverse_scale(int y) { // Imperfectly but closely enoughly reverse the volume scale Windows gives back to 1-100 using a few key points for linear translation
    if (y <= 18) {
        return round(y / 6.0);
    } else if (y <= 26) {
        return 4 + round((y - 22) / 4.0);
    } else if (y <= 56) {
        return 5 + round((y - 26) / 2.0);
    } else if (y <= 80) {
        return 20 + round((y - 56) / 0.8);
    } else if (y <= 94) {
        return 50 + round((y - 80) / 0.47);
    } else if (y <= 100) {
        return 80 + round((y - 94) / 0.3);
    } else {
        return -1;
    }
}

static esp_err_t uac_device_output_cb(uint8_t *buf, size_t len, void *arg)
{
    if (is_muted)
        return ESP_OK;
    memcpy(data_in + data_in_head, buf, len);
    data_in_head += len;
    while (data_in_head >= CHUNK_SIZE)  {
        memcpy(data_out + HEADER_SIZE, data_in, CHUNK_SIZE);
        sendto(sock, data_out, PACKET_SIZE, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        data_in_head -= CHUNK_SIZE;
        for (int i=0;i<data_in_head;i++)
            data_in[i] = data_in[i + CHUNK_SIZE];
    }
    return ESP_OK;
}

static void uac_device_set_mute_cb(uint32_t mute, void *arg)
{
    ESP_LOGI(TAG, "uac_device_set_mute_cb: %"PRIu32"", mute);
    is_muted = !!mute;
}

static void uac_device_set_volume_cb(uint32_t _volume, void *arg)
{
    _volume = reverse_scale(_volume);
    ESP_LOGI(TAG, "setting volume: %u", _volume);
    new_volume = _volume;
}

void setVolume() {
    if (new_volume != volume && connected == true) {
        ESP_LOGI(TAG, "new volume: %u volume: %u", new_volume, volume);
        char request[128];
        sprintf(request, "sources_self/volume/%.2f", (float)new_volume * .01f);
        ESP_LOGI(TAG, "uac_device_set_volume_cb: %s", request);
        http_request(request);
        volume = new_volume;
    }
}


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        connected = false;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        connected = true;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

            // Initialize UDP socket after getting IP
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            return;
        }
        
        dest_addr.sin_addr.s_addr = inet_addr(SERVER);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    memcpy(data_out, header, HEADER_SIZE);

    // Initialize and connect to Wi-Fi
    wifi_init_sta();

    uac_device_config_t config = {
        .output_cb = uac_device_output_cb,
        .set_mute_cb = uac_device_set_mute_cb,
        .set_volume_cb = uac_device_set_volume_cb,
        .cb_ctx = NULL,
    };

    ESP_LOGI(TAG, "%d", reverse_scale(6));   // Should print 1
    ESP_LOGI(TAG, "%d", reverse_scale(12));  // Should print 2
    ESP_LOGI(TAG, "%d", reverse_scale(18));  // Should print 3
    ESP_LOGI(TAG, "%d", reverse_scale(22));  // Should print 4
    ESP_LOGI(TAG, "%d", reverse_scale(26));  // Should print 5
    ESP_LOGI(TAG, "%d", reverse_scale(56));  // Should print 20
    ESP_LOGI(TAG, "%d", reverse_scale(80));  // Should print 50
    ESP_LOGI(TAG, "%d", reverse_scale(94));  // Should print 80
    ESP_LOGI(TAG, "%d", reverse_scale(100)); // Should print 100



    uac_device_init(&config);
    while (1) {
        setVolume();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
