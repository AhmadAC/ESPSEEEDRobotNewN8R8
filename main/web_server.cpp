// main\web_server.cpp
#include "web_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "WEB_FACADE";
static httpd_handle_t server = NULL;

static void delayed_reboot_task(void *pvParameter) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

void trigger_delayed_reboot() {
    xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
}

esp_err_t get_post_json(httpd_req_t *req, cJSON **json_out) {
    *json_out = NULL;
    int total_len = req->content_len;
    if (total_len >= 512 || total_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload size");
        return ESP_FAIL;
    }
 
    char* buf = (char*)malloc(total_len + 1);
    int received = 0;
    while (received < total_len) {
        int r = httpd_req_recv(req, buf + received, total_len - received);
        if (r <= 0) {
            free(buf);
            return ESP_FAIL;
        }
        received += r;
    }
    buf[total_len] = '\0';
 
    *json_out = cJSON_Parse(buf);
    free(buf);
 
    if (*json_out == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void web_server_start() {
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);

    if (server == NULL) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = 24; // Scaled down requirement
        config.max_open_sockets = 7;
        config.lru_purge_enable = true;
 
        if (httpd_start(&server, &config) == ESP_OK) {
            register_system_endpoints(server);
            register_robot_endpoints(server);
            ESP_LOGI(TAG, "Web Server started successfully on port %d", config.server_port);
        }
    }
}

void web_server_stop() {
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web Server stopped successfully to save resources.");
    }
}