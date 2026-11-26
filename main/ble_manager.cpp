// main\ble_manager.cpp
#include "ble_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "servo_controller.h"
#include "claw_controller.h"
#include "cam_controller.h"
#include "audio_player.h"
#include "sensor_monitor.h"
#include "wifi_manager.h"
#include "cJSON.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "BLE_MGR";
static const char *DEVICE_NAME = "ESP32-Robot";

extern bool is_claw_mode;
extern bool is_cam_mode;

static const ble_uuid128_t gatt_svr_svc_uart_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t gatt_svr_chr_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t gatt_svr_chr_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle = 0;
static bool is_ble_connected_flag = false;
static bool is_subscribed_flag = false;
static char current_ip[32] = "192.168.4.1";

static bool ble_sync_front = false;
static bool ble_sync_back = false;
static bool ble_sync_all = false;

#define BLE_CMD_QUEUE_LEN 16
#define BLE_CMD_MAX_LEN 128
static QueueHandle_t s_ble_cmd_queue = NULL;

static const char *DEFAULT_SWITCH_COMMANDS[16] = {
    "stop", "stand", "sit", "bark", "beep", "stop", "vol -10", "vol +10",
    "stretch_back", "stretch_down", "crawl", "leap_forward",
    "forward", "backward", "left_wave", "right_wave"
};

bool ble_manager_is_connected(void) {
    return is_ble_connected_flag;
}

void ble_manager_send_notify(const char* message) {
    if (!is_ble_connected_flag || active_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_tx_val_handle || !message) {
        return;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(message, strlen(message));
    if (om != NULL) {
        ble_gatts_notify_custom(active_conn_handle, s_tx_val_handle, om);
    }
}

void ble_manager_notify_ip(const char* ip) {
    if (ip && strlen(ip) > 0) {
        strncpy(current_ip, ip, sizeof(current_ip) - 1);
        char msg[64];
        snprintf(msg, sizeof(msg), "IP:%s\n", current_ip);
        ble_manager_send_notify(msg);
    }
}

void ble_manager_send_status(const char* status_json) {
    if (status_json) ble_manager_send_notify(status_json);
}

static void send_full_status(void) {
    if (!is_ble_connected_flag) return;

    int ll = 90, hr = 90, hl = 90, lr = 90;
    int oll = 0, ohr = 0, ohl = 0, olr = 0;
    if (!is_claw_mode && !is_cam_mode) {
        servo_get_angles(&ll, &hr, &hl, &lr);
        servo_get_offsets(&oll, &ohr, &ohl, &olr);
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "SENS:%.1f,%d,%ld\n",
             (double)sensor_get_distance(),
             sensor_is_enabled() ? 1 : 0,
             (long)sensor_get_threshold());
    ble_manager_send_notify(buf);

    snprintf(buf, sizeof(buf), "ANG:%d,%d,%d,%d\n", hl, hr, ll, lr);
    ble_manager_send_notify(buf);

    snprintf(buf, sizeof(buf), "CAL:ohl=%d,ohr=%d,oll=%d,olr=%d\n", ohl, ohr, oll, olr);
    ble_manager_send_notify(buf);

    snprintf(buf, sizeof(buf), "STATUS:vol=%d,dist=%.1f\n",
             audio_get_volume(), (double)sensor_get_distance());
    ble_manager_send_notify(buf);

    int sll = 1, shr = 2, shl = 3, slr = 4;
    int trig = 5, echo = 6;
    int bclk = 10, lrc = 11, din = 12;
    servo_get_pins(&sll, &shr, &shl, &slr);
    sensor_get_pins(&trig, &echo);
    audio_get_pins(&bclk, &lrc, &din);

    snprintf(buf, sizeof(buf), "HWPINS:srv=%d,%d,%d,%d;sens=%d,%d;aud=%d,%d,%d\n",
             sll, shr, shl, slr, trig, echo, bclk, lrc, din);
    ble_manager_send_notify(buf);

    char nhl[32] = {0}, nhr[32] = {0}, nll[32] = {0}, nlr[32] = {0};
    servo_get_leg_names(nhl, nhr, nll, nlr, sizeof(nhl));
    snprintf(buf, sizeof(buf), "HWNAMES:hl=%.31s;hr=%.31s;ll=%.31s;lr=%.31s\n", nhl, nhr, nll, nlr);
    ble_manager_send_notify(buf);
}

static void execute_ble_command(const char* p) {
    if (!p || *p == '\0') return;
    ESP_LOGI(TAG, "BLE Executing: '%s'", p);

    if (strncmp(p, "wifi:", 5) == 0 || strncmp(p, "w:", 2) == 0) {
        const char* creds = (strncmp(p, "wifi:", 5) == 0) ? (p + 5) : (p + 2);
        char* comma = strchr(creds, ',');
        if (comma) {
            char ssid[33] = {0}, pass[65] = {0};
            size_t s_len = comma - creds;
            if (s_len < sizeof(ssid)) {
                strncpy(ssid, creds, s_len);
                strncpy(pass, comma + 1, sizeof(pass) - 1);
                wifi_save_credentials(ssid, pass);
                wifi_manager_connect_async(ssid, pass);
                ble_manager_send_notify(">> Wi-Fi Saved. Connecting...\n");
                return;
            }
        }
    }

    if (strcmp(p, "sensor on") == 0 || strcmp(p, "sensor 1") == 0) {
        sensor_set_enabled(true);
        ble_manager_send_notify(">> Sensor Armed\n");
        send_full_status();
        return;
    }
    if (strcmp(p, "sensor off") == 0 || strcmp(p, "sensor 0") == 0) {
        sensor_set_enabled(false);
        ble_manager_send_notify(">> Sensor Disarmed (Passive Monitor)\n");
        send_full_status();
        return;
    }
    if (strncmp(p, "sens_thresh ", 12) == 0) {
        sensor_set_threshold(atoi(p + 12));
        send_full_status();
        return;
    }
    if (strncmp(p, "sens_actions ", 13) == 0) {
        char trip[32] = {0}, clear[32] = {0};
        if (sscanf(p + 13, "%31s %31s", trip, clear) == 2) {
            sensor_set_actions(trip, clear);
        }
        send_full_status();
        return;
    }
    if (strncmp(p, "sens_audio ", 11) == 0) {
        char atrip[32] = {0}, aclear[32] = {0};
        if (sscanf(p + 11, "%31s %31s", atrip, aclear) == 2) {
            sensor_set_audio_actions(atrip, aclear);
        }
        send_full_status();
        return;
    }

    int ll = 0, hr = 0, hl = 0, lr = 0, tr = 0, ec = 0, b = 0, l = 0, d = 0;
    if (sscanf(p, "setpins leg %d %d %d %d", &ll, &hr, &hl, &lr) == 4) {
        servo_set_pins(ll, hr, hl, lr);
        ble_manager_send_notify(">> Leg Pins Updated\n");
        send_full_status();
        return;
    }
    if (sscanf(p, "setpins sens %d %d", &tr, &ec) == 2) {
        sensor_set_pins(tr, ec);
        ble_manager_send_notify(">> Sensor Pins Updated\n");
        send_full_status();
        return;
    }
    if (sscanf(p, "setpins aud %d %d %d", &b, &l, &d) == 3) {
        audio_set_pins(b, l, d);
        ble_manager_send_notify(">> Audio Pins Updated\n");
        send_full_status();
        return;
    }
    char nhl[32] = {0}, nhr[32] = {0}, nll[32] = {0}, nlr[32] = {0};
    if (sscanf(p, "setnames %31s %31s %31s %31s", nhl, nhr, nll, nlr) == 4) {
        servo_set_leg_names(nhl, nhr, nll, nlr);
        ble_manager_send_notify(">> Leg Names Updated\n");
        send_full_status();
        return;
    }
    if (strcmp(p, "gethw") == 0) {
        send_full_status();
        return;
    }

    if (strcmp(p, "sync_front on") == 0) {
        ble_sync_front = true;
        ble_manager_send_notify(">> Front Sync ON\n");
        return;
    }
    if (strcmp(p, "sync_front off") == 0) {
        ble_sync_front = false;
        ble_manager_send_notify(">> Front Sync OFF\n");
        return;
    }
    if (strcmp(p, "sync_back on") == 0) {
        ble_sync_back = true;
        ble_manager_send_notify(">> Back Sync ON\n");
        return;
    }
    if (strcmp(p, "sync_back off") == 0) {
        ble_sync_back = false;
        ble_manager_send_notify(">> Back Sync OFF\n");
        return;
    }
    if (strcmp(p, "sync_all on") == 0) {
        ble_sync_all = ble_sync_front = ble_sync_back = true;
        ble_manager_send_notify(">> All Sync ON\n");
        return;
    }
    if (strcmp(p, "sync_all off") == 0) {
        ble_sync_all = ble_sync_front = ble_sync_back = false;
        ble_manager_send_notify(">> All Sync OFF\n");
        return;
    }

    int leg_ang = 0;
    if (sscanf(p, "hl %d", &leg_ang) == 1) {
        servo_set_target("high_left", leg_ang);
        if (ble_sync_front || ble_sync_all) {
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("high_right", leg_ang);
        }
        if (ble_sync_all) {
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("low_left", leg_ang);
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("low_right", leg_ang);
        }
        send_full_status();
        return;
    }
    if (sscanf(p, "hr %d", &leg_ang) == 1) {
        servo_set_target("high_right", leg_ang);
        if (ble_sync_front || ble_sync_all) {
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("high_left", leg_ang);
        }
        if (ble_sync_all) {
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("low_left", leg_ang);
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("low_right", leg_ang);
        }
        send_full_status();
        return;
    }
    if (sscanf(p, "ll %d", &leg_ang) == 1) {
        servo_set_target("low_left", leg_ang);
        if (ble_sync_back || ble_sync_all) {
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("low_right", leg_ang);
        }
        if (ble_sync_all) {
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("high_left", leg_ang);
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("high_right", leg_ang);
        }
        send_full_status();
        return;
    }
    if (sscanf(p, "lr %d", &leg_ang) == 1) {
        servo_set_target("low_right", leg_ang);
        if (ble_sync_back || ble_sync_all) {
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("low_left", leg_ang);
        }
        if (ble_sync_all) {
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("high_left", leg_ang);
            vTaskDelay(pdMS_TO_TICKS(120));
            servo_set_target("high_right", leg_ang);
        }
        send_full_status();
        return;
    }

    if (sscanf(p, "cal_set offsets %d %d %d %d", &ll, &hr, &hl, &lr) == 4) {
        servo_set_calibration("offsets", ll, hr, hl, lr, true);
        vTaskDelay(pdMS_TO_TICKS(40));
        servo_set_action_bypass("stand");
        ble_manager_send_notify(">> Offsets Saved\n");
        send_full_status();
        return;
    }
    if (sscanf(p, "offset ll %d", &ll) == 1) {
        int o1, o2, o3, o4;
        servo_get_offsets(&o1, &o2, &o3, &o4);
        servo_set_calibration("offsets", ll, o2, o3, o4, true);
        send_full_status();
        return;
    }
    if (sscanf(p, "offset hr %d", &hr) == 1) {
        int o1, o2, o3, o4;
        servo_get_offsets(&o1, &o2, &o3, &o4);
        servo_set_calibration("offsets", o1, hr, o3, o4, true);
        send_full_status();
        return;
    }
    if (sscanf(p, "offset hl %d", &hl) == 1) {
        int o1, o2, o3, o4;
        servo_get_offsets(&o1, &o2, &o3, &o4);
        servo_set_calibration("offsets", o1, o2, hl, o4, true);
        send_full_status();
        return;
    }
    if (sscanf(p, "offset lr %d", &lr) == 1) {
        int o1, o2, o3, o4;
        servo_get_offsets(&o1, &o2, &o3, &o4);
        servo_set_calibration("offsets", o1, o2, o3, lr, true);
        send_full_status();
        return;
    }
    if (strcmp(p, "cal_save") == 0) {
        int o1, o2, o3, o4;
        servo_get_offsets(&o1, &o2, &o3, &o4);
        servo_set_calibration("offsets", o1, o2, o3, o4, true);
        vTaskDelay(pdMS_TO_TICKS(40));
        servo_set_action_bypass("stand");
        ble_manager_send_notify(">> Calibration saved\n");
        send_full_status();
        return;
    }
    if (strcmp(p, "cal_get") == 0) {
        int o1, o2, o3, o4;
        servo_get_offsets(&o1, &o2, &o3, &o4);
        char rep[64];
        snprintf(rep, sizeof(rep), "CAL:ohl=%d,ohr=%d,oll=%d,olr=%d\n", o3, o2, o1, o4);
        ble_manager_send_notify(rep);
        return;
    }
    if (strcmp(p, "cal_reset") == 0) {
        servo_set_calibration("offsets", 0, 0, 0, 11, true);
        vTaskDelay(pdMS_TO_TICKS(40));
        servo_set_action_bypass("stand");
        ble_manager_send_notify(">> Reset defaults\n");
        send_full_status();
        return;
    }

    if (strncmp(p, "switchkey ", 10) == 0) {
        char *endptr = NULL;
        long btn_idx = strtol(p + 10, &endptr, 10);
        if (btn_idx >= 0 && btn_idx < 16 && endptr && *endptr != '\0') {
            while (*endptr == ' ' || *endptr == '\t') endptr++;
            if (*endptr != '\0') {
                nvs_handle_t handle;
                if (nvs_open("switch_cfg", NVS_READWRITE, &handle) == ESP_OK) {
                    char k[16];
                    snprintf(k, sizeof(k), "sw_%ld", btn_idx);
                    nvs_set_str(handle, k, endptr);
                    nvs_commit(handle);
                    nvs_close(handle);
                }
                char conf[128];
                snprintf(conf, sizeof(conf), "SWKEY:%ld=%.80s\n", btn_idx, endptr);
                ble_manager_send_notify(conf);
                return;
            }
        }
    }
    if (strcmp(p, "getswitchkeys") == 0) {
        nvs_handle_t handle;
        bool nvs_ok = (nvs_open("switch_cfg", NVS_READONLY, &handle) == ESP_OK);
        for (int i = 0; i < 16; i++) {
            char val[64] = {0}, k[16];
            size_t len = sizeof(val);
            snprintf(k, sizeof(k), "sw_%d", i);
            if (!nvs_ok || nvs_get_str(handle, k, val, &len) != ESP_OK) {
                strncpy(val, DEFAULT_SWITCH_COMMANDS[i], sizeof(val) - 1);
            }
            char line[128];
            snprintf(line, sizeof(line), "SWKEY:%d=%.80s\n", i, val);
            ble_manager_send_notify(line);
        }
        if (nvs_ok) nvs_close(handle);
        return;
    }
    if (strcmp(p, "switchreset") == 0) {
        nvs_handle_t handle;
        if (nvs_open("switch_cfg", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_all(handle);
            nvs_commit(handle);
            nvs_close(handle);
        }
        for (int i = 0; i < 16; i++) {
            char line[128];
            snprintf(line, sizeof(line), "SWKEY:%d=%.80s\n", i, DEFAULT_SWITCH_COMMANDS[i]);
            ble_manager_send_notify(line);
        }
        ble_manager_send_notify(">> Switch mappings reset.\n");
        return;
    }

    if (strcmp(p, "bark") == 0 || strcmp(p, "play") == 0 || strcmp(p, "dog_bark") == 0) {
        audio_play("dog_bark");
        ble_manager_send_notify(">> Dog Bark\n");
        return;
    }
    if (strcmp(p, "beep") == 0 || strcmp(p, "test") == 0 || strcmp(p, "test_beep") == 0) {
        audio_play_test_beep();
        ble_manager_send_notify(">> Test Beep\n");
        return;
    }
    if (strcmp(p, "audio_stop") == 0) {
        audio_stop();
        ble_manager_send_notify(">> Audio Stopped\n");
        return;
    }
    if (strncmp(p, "vol ", 4) == 0 || strncmp(p, "volume ", 7) == 0) {
        const char *v_str = (strncmp(p, "vol ", 4) == 0) ? (p + 4) : (p + 7);
        while (*v_str == ' ') v_str++;
        int tv = (v_str[0] == '+' || v_str[0] == '-') ? (audio_get_volume() + atoi(v_str)) : atoi(v_str);
        if (tv < 0) tv = 0;
        if (tv > 100) tv = 100;
        audio_set_volume(tv);
        char msg[64];
        snprintf(msg, sizeof(msg), ">> Volume: %d%%\n", tv);
        ble_manager_send_notify(msg);
        send_full_status();
        return;
    }

    if (strcmp(p, "status") == 0) {
        send_full_status();
        return;
    }
    if (strcmp(p, "reboot") == 0) {
        ble_manager_send_notify(">> Rebooting...\n");
        vTaskDelay(pdMS_TO_TICKS(150));
        esp_restart();
        return;
    }
    if (strcmp(p, "cam_flip") == 0) {
        cam_toggle_flip();
        ble_manager_send_notify(">> Cam Flipped\n");
        return;
    }

    bool is_num = true;
    for (int i = 0; p[i] != '\0'; i++) {
        if (!isdigit((unsigned char)p[i])) { is_num = false; break; }
    }
    if (is_num || strncmp(p, "angle ", 6) == 0) {
        int ang = is_num ? atoi(p) : atoi(p + 6);
        if (is_claw_mode) claw_set_angle(ang);
        else servo_set_target("all", ang);
        char msg[64];
        snprintf(msg, sizeof(msg), ">> Angle: %d\n", ang);
        ble_manager_send_notify(msg);
        send_full_status();
        return;
    }

    cJSON *json = cJSON_Parse(p);
    if (json) {
        cJSON *act_item = cJSON_GetObjectItem(json, "action");
        if (act_item && act_item->valuestring) {
            if (is_claw_mode) claw_execute_command(act_item->valuestring);
            else servo_set_action(act_item->valuestring);
        }
        cJSON *sen_en = cJSON_GetObjectItem(json, "sensor_enabled");
        if (sen_en) sensor_set_enabled(cJSON_IsTrue(sen_en) || sen_en->valueint != 0);

        cJSON *sen_th = cJSON_GetObjectItem(json, "sensor_threshold");
        if (sen_th) sensor_set_threshold(sen_th->valueint);

        cJSON *sen_trip = cJSON_GetObjectItem(json, "sensor_tripped_action");
        cJSON *sen_clear = cJSON_GetObjectItem(json, "sensor_cleared_action");
        if (sen_trip && sen_clear && sen_trip->valuestring && sen_clear->valuestring) {
            sensor_set_actions(sen_trip->valuestring, sen_clear->valuestring);
        }
        cJSON *sen_atrip = cJSON_GetObjectItem(json, "sensor_tripped_audio");
        cJSON *sen_aclear = cJSON_GetObjectItem(json, "sensor_cleared_audio");
        if (sen_atrip && sen_aclear && sen_atrip->valuestring && sen_aclear->valuestring) {
            sensor_set_audio_actions(sen_atrip->valuestring, sen_aclear->valuestring);
        }
        cJSON_Delete(json);
        ble_manager_send_notify(">> JSON Handled\n");
        send_full_status();
        return;
    }

    if (is_claw_mode) claw_execute_command(p);
    else servo_set_action(p);

    char reply[128];
    snprintf(reply, sizeof(reply), ">> Action: %.90s\n", p);
    ble_manager_send_notify(reply);
}

static void ble_cmd_worker_task(void *pvParameters) {
    char cmd_buf[BLE_CMD_MAX_LEN];
    TickType_t last_tick = xTaskGetTickCount();
    while (1) {
        if (xQueueReceive(s_ble_cmd_queue, cmd_buf, pdMS_TO_TICKS(350)) == pdTRUE) {
            execute_ble_command(cmd_buf);
        }
        if (is_ble_connected_flag) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_tick) >= pdMS_TO_TICKS(350)) {
                last_tick = now;
                char pkt[48];
                snprintf(pkt, sizeof(pkt), "SENS:%.1f,%d,%ld\n",
                         (double)sensor_get_distance(),
                         sensor_is_enabled() ? 1 : 0,
                         (long)sensor_get_threshold());
                ble_manager_send_notify(pkt);
            }
        }
    }
}

static int gatt_svr_chr_access_rx(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        char buf[BLE_CMD_MAX_LEN];
        if (om_len >= sizeof(buf)) om_len = sizeof(buf) - 1;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, NULL) == 0) {
            buf[om_len] = '\0';
            while (om_len > 0 && (buf[om_len - 1] == '\r' || buf[om_len - 1] == '\n')) buf[--om_len] = '\0';
            char *ptr = buf;
            while (*ptr == ' ' || *ptr == '\t') ptr++;
            if (*ptr != '\0' && s_ble_cmd_queue) xQueueSend(s_ble_cmd_queue, ptr, 0);
        }
    }
    return 0;
}

static int gatt_svr_chr_access_tx(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char s_data[64];
        snprintf(s_data, sizeof(s_data), "SENS:%.1f,%d,%ld\n",
                 (double)sensor_get_distance(),
                 sensor_is_enabled() ? 1 : 0,
                 (long)sensor_get_threshold());
        return os_mbuf_append(ctxt->om, s_data, strlen(s_data)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static const struct ble_gatt_chr_def gatt_svr_chrs[] = {
    {
        .uuid = &gatt_svr_chr_rx_uuid.u,
        .access_cb = gatt_svr_chr_access_rx,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP
    },
    {
        .uuid = &gatt_svr_chr_tx_uuid.u,
        .access_cb = gatt_svr_chr_access_tx,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_val_handle
    },
    {}
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uart_uuid.u,
        .characteristics = gatt_svr_chrs
    },
    {}
};

static void ble_advertise(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status == 0) {
            active_conn_handle = event->connect.conn_handle;
            is_ble_connected_flag = true;
            is_subscribed_flag = false;
        } else {
            active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            is_ble_connected_flag = is_subscribed_flag = false;
            ble_advertise();
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        is_ble_connected_flag = is_subscribed_flag = false;
        ble_advertise();
    } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE) {
        is_subscribed_flag = (event->subscribe.cur_notify != 0);
        if (is_subscribed_flag && s_ble_cmd_queue) {
            const char *trig = "status";
            xQueueSend(s_ble_cmd_queue, trig, 0);
        }
    }
    return 0;
}

static void ble_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields, rsp_fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = (ble_uuid128_t *)&gatt_svr_svc_uart_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp_fields);

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void) {
    ble_advertise();
}

static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE reset: %d", reason);
}

void ble_manager_init(void) {
    if (!s_ble_cmd_queue) {
        s_ble_cmd_queue = xQueueCreate(BLE_CMD_QUEUE_LEN, BLE_CMD_MAX_LEN);
        xTaskCreatePinnedToCore(ble_cmd_worker_task, "ble_worker", 8192, NULL, 5, NULL, 1);
    }
    nimble_port_init();
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_att_set_preferred_mtu(512);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE Manager Initialized.");
}