

// main\web_api_robot.cpp
#include "web_server.h"
#include "servo_controller.h"
#include "claw_controller.h"
#include "cam_controller.h"
#include "audio_player.h"
#include "sensor_monitor.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

extern bool is_claw_mode;
extern bool is_cam_mode;

static esp_err_t claw_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    char buf[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
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

static esp_err_t leg_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char id[24] = {0};
        char angle_str[16] = {0};
        if (httpd_query_key_value(query, "id", id, sizeof(id)) == ESP_OK &&
            httpd_query_key_value(query, "angle", angle_str, sizeof(angle_str)) == ESP_OK) {
            servo_set_target(id, atoi(angle_str));
            httpd_resp_sendstr(req, "OK");
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing id/angle param");
    return ESP_FAIL;
}

static esp_err_t calibration_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    char *json_str = servo_get_calibrations_json();
    if (json_str) {
        httpd_resp_send(req, json_str, strlen(json_str));
        free(json_str);
    } else {
        httpd_resp_sendstr(req, "{}");
    }
    return ESP_OK;
}

static esp_err_t calibration_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *target = cJSON_GetObjectItem(json, "target");
        cJSON *ll = cJSON_GetObjectItem(json, "ll");
        cJSON *hr = cJSON_GetObjectItem(json, "hr");
        cJSON *hl = cJSON_GetObjectItem(json, "hl");
        cJSON *lr = cJSON_GetObjectItem(json, "lr");
        cJSON *save = cJSON_GetObjectItem(json, "save");

        if (target && target->valuestring && ll && hr && hl && lr) {
            bool save_to_nvs = save ? (cJSON_IsTrue(save) || save->valueint != 0) : true;
            servo_set_calibration(target->valuestring, ll->valueint, hr->valueint, hl->valueint, lr->valueint, save_to_nvs);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t sensor_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", sensor_is_enabled());
    cJSON_AddNumberToObject(root, "distance", (double)sensor_get_distance());
    cJSON_AddNumberToObject(root, "threshold", sensor_get_threshold());
    cJSON_AddNumberToObject(root, "reaction_ms", sensor_get_reaction_time());
    cJSON_AddStringToObject(root, "tripped_action", sensor_get_tripped_action());
    cJSON_AddStringToObject(root, "cleared_action", sensor_get_cleared_action());
    cJSON_AddStringToObject(root, "tripped_audio", sensor_get_tripped_audio());
    cJSON_AddStringToObject(root, "cleared_audio", sensor_get_cleared_audio());

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

static esp_err_t sensor_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *en = cJSON_GetObjectItem(json, "enabled");
        if (en) sensor_set_enabled(cJSON_IsTrue(en) || en->valueint != 0);

        cJSON *th = cJSON_GetObjectItem(json, "threshold");
        if (th) sensor_set_threshold(th->valueint);

        cJSON *trip_act = cJSON_GetObjectItem(json, "tripped_action");
        cJSON *clear_act = cJSON_GetObjectItem(json, "cleared_action");
        if (trip_act && clear_act) sensor_set_actions(trip_act->valuestring, clear_act->valuestring);

        cJSON *trip_aud = cJSON_GetObjectItem(json, "tripped_audio");
        cJSON *clear_aud = cJSON_GetObjectItem(json, "cleared_audio");
        if (trip_aud && clear_aud) sensor_set_audio_actions(trip_aud->valuestring, clear_aud->valuestring);

        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }
    return ESP_FAIL;
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
        return ESP_OK;
    }
    return ESP_FAIL;
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

    uri = {}; uri.uri = "/leg"; uri.method = HTTP_GET; uri.handler = leg_get_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/calibration"; uri.method = HTTP_GET; uri.handler = calibration_get_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/calibration"; uri.method = HTTP_POST; uri.handler = calibration_post_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/sensor"; uri.method = HTTP_GET; uri.handler = sensor_get_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/sensor"; uri.method = HTTP_POST; uri.handler = sensor_post_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/action"; uri.method = HTTP_POST; uri.handler = action_post_handler;
    httpd_register_uri_handler(server, &uri);

    uri = {}; uri.uri = "/beep"; uri.method = HTTP_GET; uri.handler = play_beep_handler;
    httpd_register_uri_handler(server, &uri);
}

