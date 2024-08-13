#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "secrets.h"
#include "esp_crt_bundle.h"

#define MAX_HTTP_OUTPUT_BUFFER 256

static const char *TAG = "HTTPS_CLIENT";

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

void http_request(const char *endpoint)
{
    char url[256];
    sprintf(url, "%s%s", HTTPS_SERVER_URL, endpoint);
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,  // Use HTTPS
        .cert_pem = NULL,  // Set to NULL to use the default bundle
        .skip_cert_common_name_check = true,  // Skip certificate common name check
        .crt_bundle_attach = NULL,
        .disable_auto_redirect = true,  // Disable auto-redirect
        .timeout_ms = 5000,  // Set a timeout (5 seconds in this example)
        .buffer_size = 256,  // Increase buffer size if needed
        .buffer_size_tx = 256,  // Increase transmit buffer size if needed
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return;
    }
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        ESP_LOGI(TAG, "Response: %s", local_response_buffer);
    } else if (err == ESP_ERR_HTTP_CONNECT) {
        ESP_LOGE(TAG, "Failed to resolve domain or connect to server");
    } else {
        ESP_LOGE(TAG, "HTTPS GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}