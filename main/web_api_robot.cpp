// main\web_api_robot.cpp
#include "web_server.h"
#include "servo_controller.h"
#include "claw_controller.h"
#include "cam_controller.h"
#include "audio_player.h"
#include "sensor_monitor.h"
#include "esp_log.h"
#include <string.h>

extern bool is_claw_mode;
extern bool is_cam_mode;

static esp_err_t claw_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    char buf[50];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[20];
        if (httpd_query_key_value(buf, "cmd", param, sizeof(param)) == ESP_OK) {
            claw_execute_command(param);
            httpd_resp_sendstr(req, "Command Sent");
            return ESP_OK;
        }
        if (httpd_query_key_value(buf, "angle", param, sizeof(param)) == ESP_OK) {
            claw_set_angle(atoi(param));
            httpd_resp_sendstr(req, "Angle Set");
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing cmd/angle param");
    return ESP_FAIL;
}

static esp_err_t play_beep_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    audio_play_test_beep();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "BEEPING");
    return ESP_OK;
}

static esp_err_t action_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *act_item = cJSON_GetObjectItem(json, "action");
        if (act_item) {
            const char* act = act_item->valuestring;
            if (is_claw_mode || strcmp(act, "open") == 0 || strcmp(act, "close") == 0) {
                claw_execute_command(act);
            } else {
                servo_set_action(act);
            }
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &ws_pkt, 0) != ESP_OK) return ESP_FAIL;
 
    if (ws_pkt.len) {
        uint8_t *buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (!buf) return ESP_ERR_NO_MEM;
        ws_pkt.payload = buf;
        if (httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len) == ESP_OK) {
            cJSON *json = cJSON_Parse((char*)ws_pkt.payload);
            if (json) {
                cJSON *act_item = cJSON_GetObjectItem(json, "action");
                if (act_item) {
                    if (is_claw_mode) claw_execute_command(act_item->valuestring);
                    else servo_set_action(act_item->valuestring);
                }
                cJSON_Delete(json);
            }
        }
        free(buf);
    }
    return ESP_OK;
}

void register_robot_endpoints(httpd_handle_t server) {
    httpd_uri_t uri = {};

    uri = {}; uri.uri = "/ws"; uri.method = HTTP_GET; uri.handler = ws_handler; uri.is_websocket = true;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/claw"; uri.method = HTTP_GET; uri.handler = claw_get_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/action"; uri.method = HTTP_POST; uri.handler = action_post_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/beep"; uri.method = HTTP_GET; uri.handler = play_beep_handler;
    httpd_register_uri_handler(server, &uri);
}