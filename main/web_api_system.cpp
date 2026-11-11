// main\web_api_system.cpp
#include "web_server.h"
#include "web_html.h"
#include "wifi_manager.h"
#include "logger.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

extern bool is_claw_mode;
extern bool is_cam_mode;

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "close");

    if (is_claw_mode) {
        httpd_resp_send(req, HTML_CLAW_BASIC, HTTPD_RESP_USE_STRLEN);
    } else if (is_cam_mode) {
        httpd_resp_send(req, HTML_CAM_BASIC, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, HTML_ROBOT_BASIC, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

static esp_err_t captive_portal_redirect(httpd_req_t *req) {
    char host[64] = {0};
    httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    if (strcmp(host, "192.168.4.1") == 0 || strcmp(host, "192.168.4.1:80") == 0) {
        if (strcmp(req->uri, "/") == 0 || strcmp(req->uri, "/generate_204") == 0) {
            return index_get_handler(req);
        }
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    char* json_str = wifi_scan_networks_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
        cJSON *pass_item = cJSON_GetObjectItem(json, "pass");
        if (ssid_item && pass_item) {
            wifi_save_credentials(ssid_item->valuestring, pass_item->valuestring);
            nvs_handle_t my_handle;
            if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
                nvs_set_u8(my_handle, "force_ap", 0);
                nvs_commit(my_handle);
                nvs_close(my_handle);
            }
            trigger_delayed_reboot();
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t switch_mode_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *mode_item = cJSON_GetObjectItem(json, "mode");
        if (mode_item && mode_item->valuestring) {
            nvs_handle_t my_handle;
            if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
                nvs_set_str(my_handle, "dev_mode", mode_item->valuestring);
                nvs_commit(my_handle);
                nvs_close(my_handle);
            }
            trigger_delayed_reboot();
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t reset_cal_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_erase_all(my_handle);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    trigger_delayed_reboot();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

void register_system_endpoints(httpd_handle_t server) {
    httpd_uri_t uri = {};

    uri = {}; uri.uri = "/generate_204"; uri.method = HTTP_GET; uri.handler = captive_portal_redirect;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/"; uri.method = HTTP_GET; uri.handler = index_get_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/scan"; uri.method = HTTP_GET; uri.handler = scan_get_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/save"; uri.method = HTTP_POST; uri.handler = save_post_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/switch_mode"; uri.method = HTTP_POST; uri.handler = switch_mode_post_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/reset_cal"; uri.method = HTTP_POST; uri.handler = reset_cal_post_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/*"; uri.method = HTTP_GET; uri.handler = captive_portal_redirect;
    httpd_register_uri_handler(server, &uri);
}