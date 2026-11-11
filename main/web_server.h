// main\web_server.h
#pragma once
#include "esp_http_server.h"
#include "esp_err.h"
#include "cJSON.h"

void web_server_start();
void web_server_stop();
void trigger_delayed_reboot();

// Shared Utility for JSON requests
esp_err_t get_post_json(httpd_req_t *req, cJSON **json_out);

// Facade API Registrars
void register_system_endpoints(httpd_handle_t server);
void register_robot_endpoints(httpd_handle_t server);