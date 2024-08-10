
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
#include "secrets.h"

const char header[] = {1, 16, 2, 0, 0}; // Stereo 16 bit 48KHz default layout

#define HEADER_SIZE sizeof(header)

#define CHUNK_SIZE 1152

#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE)

static const char *TAG = "ESP32SUSBScreamSender";

bool is_muted = false;

char data_out[PACKET_SIZE];

char data_in[CHUNK_SIZE * 10];
int data_in_head = 0;

int sock = -1;
struct sockaddr_in dest_addr;


static esp_err_t uac_device_output_cb(uint8_t *buf, size_t len, void *arg)
{
    if (is_muted)
        return ESP_OK;
    memcpy(data_in + data_in_head, buf, len);
    data_in_head += len;
    while (data_in_head >= CHUNK_SIZE)  {
        memcpy(data_out + HEADER_SIZE, data_in, CHUNK_SIZE);
        sendto(sock, data_out, PACKET_SIZE, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        for (int i=0;i<CHUNK_SIZE;i++)
            data_in[i] = data_in[i + CHUNK_SIZE];
        data_in_head -= CHUNK_SIZE;
    }
    return ESP_OK;
}

static void uac_device_set_mute_cb(uint32_t mute, void *arg)
{
    ESP_LOGI(TAG, "uac_device_set_mute_cb: %"PRIu32"", mute);
    is_muted = !!mute;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
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
    };

    uac_device_init(&config);
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
