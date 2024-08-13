#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"

#define MAX_HTTP_OUTPUT_BUFFER 2048

esp_err_t _http_event_handler(esp_http_client_event_t *evt);
void http_request(const char *endpoint);

#endif // HTTP_CLIENT_H
