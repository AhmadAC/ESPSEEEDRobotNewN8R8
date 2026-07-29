// main\web_server.cpp
#include "web_server.h"
#include "web_html.h"
#include "wifi_manager.h"
#include "sensor_monitor.h"
#include "servo_controller.h"
#include "claw_controller.h"
#include "cam_controller.h"
#include "audio_player.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "WEB";
httpd_handle_t server = NULL;

extern bool is_claw_mode; 
extern bool is_cam_mode; 

static void delayed_reboot_task(void *pvParameter) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static esp_err_t index_get_handler(httpd_req_t *req) {
    if (is_cam_mode) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/app");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "close"); 

    if (is_claw_mode) {
        httpd_resp_send(req, HTML_CLAW_UI, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    nvs_handle_t my_handle;
    uint8_t sound_en = 1; 
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_u8(my_handle, "sound_en", &sound_en);
        nvs_close(my_handle);
    }
    
    httpd_resp_send_chunk(req, html_part1, HTTPD_RESP_USE_STRLEN);
    
    // Send the Robot Camera UI Card
    httpd_resp_send_chunk(req, html_cam_card, HTTPD_RESP_USE_STRLEN);
    
    if (sound_en) {
        httpd_resp_send_chunk(req, html_audio_card, HTTPD_RESP_USE_STRLEN);
    }
    
    httpd_resp_send_chunk(req, html_part2, HTTPD_RESP_USE_STRLEN);
    
    if (sound_en) {
        httpd_resp_send_chunk(req, html_audio_tripped, HTTPD_RESP_USE_STRLEN);
    }
    
    httpd_resp_send_chunk(req, html_part3, HTTPD_RESP_USE_STRLEN);
    
    if (sound_en) {
        httpd_resp_send_chunk(req, html_audio_cleared, HTTPD_RESP_USE_STRLEN);
    }
    
    httpd_resp_send_chunk(req, html_part4, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0); 
    
    return ESP_OK;
}

static esp_err_t cam_setup_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, HTML_CAM_SETUP, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t cam_app_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, HTML_CAM_APP, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t cam_capture_get_handler(httpd_req_t *req) {
    isCapturing = true; 
    vTaskDelay(pdMS_TO_TICKS(200)); 
    
    sensor_t * s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_framesize(s, FRAMESIZE_UXGA); 
        s->set_quality(s, 12); // Restored to extremely stable safe quality threshold             
    }
    
    // Clear out residual pipeline framebuffers so mode switch remains perfectly aligned
    for (int i = 0; i < 2; i++) {
        camera_fb_t * fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }
    vTaskDelay(pdMS_TO_TICKS(200)); 

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture Failed");
    } else {
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"snapshot.jpg\"");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, (const char*)fb->buf, fb->len);
        esp_camera_fb_return(fb);
    }

    if (s != NULL) {
        s->set_framesize(s, FRAMESIZE_QVGA);
        s->set_quality(s, 14);
    }
    
    isCapturing = false;
    return ESP_OK;
}

static esp_err_t cam_flip_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cam_toggle_flip();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t captive_portal_redirect(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close"); 
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t get_post_json(httpd_req_t *req, cJSON **json_out) {
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

// -----------------------------------------------------------------
// Common Endpoints
// -----------------------------------------------------------------
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

            xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t switch_ap_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, "force_ap", 1);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    httpd_resp_sendstr(req, "OK");
    xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t switch_wifi_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, "force_ap", 0);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    httpd_resp_sendstr(req, "OK");
    xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
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
            xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t espnow_pair_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cam_espnow_pair_claw();
    httpd_resp_sendstr(req, "Pairing Broadcast Sent");
    return ESP_OK;
}

// -----------------------------------------------------------------
// WebSocket Gamepad Handler
// -----------------------------------------------------------------
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket Handshake Successful");
        return ESP_OK;
    }
    
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) { return ret; }
    
    if (ws_pkt.len) {
        uint8_t *buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;
        ws_pkt.payload = buf;
        
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK) {
            cJSON *json = cJSON_Parse((char*)ws_pkt.payload);
            if (json) {
                cJSON *act_item = cJSON_GetObjectItem(json, "action");
                if (act_item && act_item->valuestring) {
                    // Instantly trigger robot animations
                    servo_set_action(act_item->valuestring);
                }
                cJSON_Delete(json);
            }
        }
        free(buf);
    }
    return ret;
}

// -----------------------------------------------------------------
// Claw Endpoints
// -----------------------------------------------------------------
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
            int angle = atoi(param);
            claw_set_angle(angle);
            snprintf(claw_last_command_str, sizeof(claw_last_command_str), "ANGLE: %d°", angle);
            httpd_resp_sendstr(req, "Angle Set");
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing cmd or angle parameter");
    return ESP_FAIL;
}

static esp_err_t claw_status_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "last_cmd", claw_last_command_str);
    cJSON_AddNumberToObject(root, "angle", claw_current_angle);
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// -------------------------------------------------------------
// Robot Endpoints
// -------------------------------------------------------------
static esp_err_t audio_config_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *vol_item = cJSON_GetObjectItem(json, "volume");
        if (vol_item) {
            audio_set_volume(vol_item->valueint);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t audio_play_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *snd_item = cJSON_GetObjectItem(json, "sound");
        if (snd_item) {
            audio_play(snd_item->valuestring);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t audio_stop_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    audio_stop();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t servo_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *ll = cJSON_GetObjectItem(json, "ll");
        cJSON *lr = cJSON_GetObjectItem(json, "lr");
        cJSON *hl = cJSON_GetObjectItem(json, "hl");
        cJSON *hr = cJSON_GetObjectItem(json, "hr");
        
        cJSON *id_item = cJSON_GetObjectItem(json, "id");
        cJSON *angle_item = cJSON_GetObjectItem(json, "angle");
        
        if (ll || lr || hl || hr) {
            if (ll) servo_set_target_silent("low_left", ll->valueint);
            if (lr) servo_set_target_silent("low_right", lr->valueint);
            if (hl) servo_set_target_silent("high_left", hl->valueint);
            if (hr) servo_set_target_silent("high_right", hr->valueint);
            servo_apply_targets();
        } else if (id_item && angle_item) {
            servo_set_target(id_item->valuestring, angle_item->valueint);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t action_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *act_item = cJSON_GetObjectItem(json, "action");
        if (act_item) {
            ESP_LOGI(TAG, "UI Triggered Action: %s", act_item->valuestring);
            servo_set_action(act_item->valuestring);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t calibrations_json_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    char* json_str = servo_get_calibrations_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

static esp_err_t download_cal_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    char* cpp_code = servo_get_calibrations_cpp();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"esprobot_defaults.txt\"");
    httpd_resp_send(req, cpp_code, strlen(cpp_code));
    free(cpp_code);
    return ESP_OK;
}

static esp_err_t calibrate_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *tgt_item = cJSON_GetObjectItem(json, "target");
        cJSON *ll_item = cJSON_GetObjectItem(json, "ll");
        cJSON *hr_item = cJSON_GetObjectItem(json, "hr");
        cJSON *hl_item = cJSON_GetObjectItem(json, "hl");
        cJSON *lr_item = cJSON_GetObjectItem(json, "lr");
        cJSON *sv_item = cJSON_GetObjectItem(json, "save");

        if (tgt_item && ll_item && hr_item && hl_item && lr_item) {
            bool save_to_nvs = sv_item ? (cJSON_IsTrue(sv_item) || sv_item->valueint != 0) : false;
            servo_set_calibration(tgt_item->valuestring, ll_item->valueint, hr_item->valueint, hl_item->valueint, lr_item->valueint, save_to_nvs);
        }
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t sensor_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *json = NULL;
    if (get_post_json(req, &json) == ESP_OK) {
        cJSON *en_item = cJSON_GetObjectItem(json, "enabled");
        if (en_item) {
            sensor_set_enabled(cJSON_IsTrue(en_item) || (en_item->valueint != 0));
        }
        cJSON *th_item = cJSON_GetObjectItem(json, "threshold");
        if (th_item) {
            sensor_set_threshold(th_item->valueint);
        }
        cJSON *re_item = cJSON_GetObjectItem(json, "reaction_time");
        if (re_item) {
            sensor_set_reaction_time(re_item->valueint);
        }
        cJSON *trip_item = cJSON_GetObjectItem(json, "tripped_action");
        cJSON *clear_item = cJSON_GetObjectItem(json, "cleared_action");
        if (trip_item && clear_item) {
            sensor_set_actions(trip_item->valuestring, clear_item->valuestring);
        }
        
        cJSON *atrip_item = cJSON_GetObjectItem(json, "tripped_audio");
        cJSON *aclear_item = cJSON_GetObjectItem(json, "cleared_audio");
        if (atrip_item && aclear_item) {
            sensor_set_audio_actions(atrip_item->valuestring, aclear_item->valuestring);
        }
        
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

static esp_err_t angles_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    
    int ang_ll, ang_hr, ang_hl, ang_lr;
    int off_ll, off_hr, off_hl, off_lr;
    
    servo_get_angles(&ang_ll, &ang_hr, &ang_hl, &ang_lr);
    servo_get_offsets(&off_ll, &off_hr, &off_hl, &off_lr);

    cJSON *root = cJSON_CreateObject();
    
    cJSON *ll = cJSON_CreateObject(); cJSON_AddNumberToObject(ll, "angle", ang_ll); cJSON_AddNumberToObject(ll, "offset", off_ll); cJSON_AddItemToObject(root, "low_left", ll);
    cJSON *hr = cJSON_CreateObject(); cJSON_AddNumberToObject(hr, "angle", ang_hr); cJSON_AddNumberToObject(hr, "offset", off_hr); cJSON_AddItemToObject(root, "high_right", hr);
    cJSON *hl = cJSON_CreateObject(); cJSON_AddNumberToObject(hl, "angle", ang_hl); cJSON_AddNumberToObject(hl, "offset", off_hl); cJSON_AddItemToObject(root, "high_left", hl);
    cJSON *lr = cJSON_CreateObject(); cJSON_AddNumberToObject(lr, "angle", ang_lr); cJSON_AddNumberToObject(lr, "offset", off_lr); cJSON_AddItemToObject(root, "low_right", lr);

    cJSON_AddBoolToObject(root, "sensor_enabled", sensor_is_enabled());
    cJSON_AddBoolToObject(root, "safety_lock", sensor_is_safety_locked());
    cJSON_AddNumberToObject(root, "sensor_distance", sensor_get_distance());
    cJSON_AddNumberToObject(root, "sensor_threshold", sensor_get_threshold());
    cJSON_AddNumberToObject(root, "sensor_reaction_time", sensor_get_reaction_time());
    
    cJSON_AddStringToObject(root, "sensor_tripped_action", sensor_get_tripped_action());
    cJSON_AddStringToObject(root, "sensor_cleared_action", sensor_get_cleared_action());
    cJSON_AddStringToObject(root, "sensor_tripped_audio", sensor_get_tripped_audio());
    cJSON_AddStringToObject(root, "sensor_cleared_audio", sensor_get_cleared_audio());
    
    cJSON_AddNumberToObject(root, "audio_volume", audio_get_volume());

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    
    httpd_resp_send(req, json_str, strlen(json_str));
    
    cJSON_Delete(root);
    free(json_str);
    return ESP_OK;
}

static esp_err_t reset_cal_post_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_erase_all(my_handle);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGW(TAG, "NVS Storage Erased. Rebooting...");
    }
    xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void web_server_init() {
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    if (server == NULL) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = 25; 
        config.max_open_sockets = 13;
        config.lru_purge_enable = true;                 
        
        if (httpd_start(&server, &config) == ESP_OK) {
            
            httpd_uri_t uri_cp1      = { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_uri_t uri_cp2      = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_uri_t uri_cp3      = { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            
            httpd_register_uri_handler(server, &uri_cp1);
            httpd_register_uri_handler(server, &uri_cp2);
            httpd_register_uri_handler(server, &uri_cp3);

            // Universal Endpoints
            httpd_uri_t uri_index    = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
            httpd_uri_t uri_scan     = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler, .user_ctx = NULL };
            httpd_uri_t uri_save     = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler, .user_ctx = NULL };
            httpd_uri_t uri_switchap = { .uri = "/switch_to_ap", .method = HTTP_POST, .handler = switch_ap_post_handler, .user_ctx = NULL };
            httpd_uri_t uri_switchwi = { .uri = "/switch_to_wifi", .method = HTTP_POST, .handler = switch_wifi_post_handler, .user_ctx = NULL };
            httpd_uri_t uri_swmode   = { .uri = "/switch_mode", .method = HTTP_POST, .handler = switch_mode_post_handler, .user_ctx = NULL };
            httpd_uri_t uri_favicon  = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler, .user_ctx = NULL };

            httpd_register_uri_handler(server, &uri_index);
            httpd_register_uri_handler(server, &uri_scan);
            httpd_register_uri_handler(server, &uri_save);
            httpd_register_uri_handler(server, &uri_switchap);
            httpd_register_uri_handler(server, &uri_switchwi);
            httpd_register_uri_handler(server, &uri_swmode);
            httpd_register_uri_handler(server, &uri_favicon);

            if (is_claw_mode) {
                httpd_uri_t uri_claw   = { .uri = "/claw",   .method = HTTP_GET, .handler = claw_get_handler,   .user_ctx = NULL };
                httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET, .handler = claw_status_get_handler, .user_ctx = NULL };
                httpd_uri_t uri_ccap   = { .uri = "/capture",.method = HTTP_GET, .handler = cam_capture_get_handler, .user_ctx = NULL };
                httpd_uri_t uri_pair   = { .uri = "/espnow_pair",.method = HTTP_POST, .handler = espnow_pair_post_handler, .user_ctx = NULL };
                
                httpd_register_uri_handler(server, &uri_claw);
                httpd_register_uri_handler(server, &uri_status);
                httpd_register_uri_handler(server, &uri_ccap);
                httpd_register_uri_handler(server, &uri_pair);
            } else if (is_cam_mode) {
                httpd_uri_t uri_cset   = { .uri = "/setup",  .method = HTTP_GET, .handler = cam_setup_get_handler, .user_ctx = NULL };
                httpd_uri_t uri_capp   = { .uri = "/app",    .method = HTTP_GET, .handler = cam_app_get_handler,   .user_ctx = NULL };
                httpd_uri_t uri_ccap   = { .uri = "/capture",.method = HTTP_GET, .handler = cam_capture_get_handler, .user_ctx = NULL };
                httpd_uri_t uri_resetcal = { .uri = "/reset_cal", .method = HTTP_POST, .handler = reset_cal_post_handler, .user_ctx = NULL };
                httpd_register_uri_handler(server, &uri_cset);
                httpd_register_uri_handler(server, &uri_capp);
                httpd_register_uri_handler(server, &uri_ccap);
                httpd_register_uri_handler(server, &uri_resetcal);
            } else {
                httpd_uri_t uri_ws       = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = NULL, .is_websocket = true };
                httpd_uri_t uri_servo    = { .uri = "/servo", .method = HTTP_POST, .handler = servo_post_handler, .user_ctx = NULL };
                httpd_uri_t uri_act      = { .uri = "/action", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
                httpd_uri_t uri_cal      = { .uri = "/calibrate", .method = HTTP_POST, .handler = calibrate_post_handler, .user_ctx = NULL };
                httpd_uri_t uri_caljson  = { .uri = "/calibrations_json", .method = HTTP_GET, .handler = calibrations_json_get_handler, .user_ctx = NULL };
                httpd_uri_t uri_dlcal    = { .uri = "/download_cal", .method = HTTP_GET, .handler = download_cal_get_handler, .user_ctx = NULL };
                httpd_uri_t uri_angs     = { .uri = "/angles", .method = HTTP_GET, .handler = angles_get_handler, .user_ctx = NULL };
                httpd_uri_t uri_sensor   = { .uri = "/sensor", .method = HTTP_POST, .handler = sensor_post_handler, .user_ctx = NULL };
                httpd_uri_t uri_resetcal = { .uri = "/reset_cal", .method = HTTP_POST, .handler = reset_cal_post_handler, .user_ctx = NULL };
                httpd_uri_t uri_audcfg   = { .uri = "/audio_config", .method = HTTP_POST, .handler = audio_config_post_handler, .user_ctx = NULL };
                httpd_uri_t uri_audply   = { .uri = "/audio_play", .method = HTTP_POST, .handler = audio_play_post_handler, .user_ctx = NULL };
                httpd_uri_t uri_audstp   = { .uri = "/audio_stop", .method = HTTP_POST, .handler = audio_stop_post_handler, .user_ctx = NULL };
                httpd_uri_t uri_ccap     = { .uri = "/capture", .method = HTTP_GET, .handler = cam_capture_get_handler, .user_ctx = NULL };
                httpd_uri_t uri_cflip    = { .uri = "/cam_flip", .method = HTTP_POST, .handler = cam_flip_post_handler, .user_ctx = NULL };
                
                httpd_register_uri_handler(server, &uri_ws);
                httpd_register_uri_handler(server, &uri_servo);
                httpd_register_uri_handler(server, &uri_act);
                httpd_register_uri_handler(server, &uri_cal);
                httpd_register_uri_handler(server, &uri_caljson);
                httpd_register_uri_handler(server, &uri_dlcal);
                httpd_register_uri_handler(server, &uri_angs);
                httpd_register_uri_handler(server, &uri_sensor);
                httpd_register_uri_handler(server, &uri_resetcal);
                httpd_register_uri_handler(server, &uri_audcfg);
                httpd_register_uri_handler(server, &uri_audply);
                httpd_register_uri_handler(server, &uri_audstp);
                httpd_register_uri_handler(server, &uri_ccap);
                httpd_register_uri_handler(server, &uri_cflip);
            }
            
            httpd_uri_t uri_fallback = { .uri = "/*", .method = HTTP_GET, .handler = captive_portal_redirect, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_fallback);
            
            ESP_LOGI(TAG, "Dashboard Server initialized successfully on port %d", config.server_port);
        }
    }
}