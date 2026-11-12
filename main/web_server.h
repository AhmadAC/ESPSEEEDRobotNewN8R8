// main\web_server.h
#pragma once
#include "esp_http_server.h"
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

void web_server_init(void);
void web_server_start(void);
void web_server_stop(void);
void trigger_delayed_reboot(void);

// Shared Utility for JSON requests
esp_err_t get_post_json(httpd_req_t *req, cJSON **json_out);

// Facade API Registrars
void register_system_endpoints(httpd_handle_t server);
void register_robot_endpoints(httpd_handle_t server);

#ifdef __cplusplus
}
#endif