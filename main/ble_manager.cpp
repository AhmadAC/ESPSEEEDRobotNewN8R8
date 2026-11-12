// 11-3ESPSEEEDRobotNewN8R8/main/ble_manager.cpp
#include "ble_manager.h"
#include "esp_log.h"
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
static char current_ip[32] = "192.168.4.1";

static const char *DEFAULT_SWITCH_COMMANDS[16] = {
    "stop", "stand", "sit", "bark",
    "beep", "stop", "vol -10", "vol +10",
    "stretch_back", "stretch_down", "crawl", "leap_forward",
    "forward", "backward", "left_wave", "right_wave"
};

bool ble_manager_is_connected(void) {
    return is_ble_connected_flag;
}

void ble_manager_send_notify(const char* message) {
    if (!is_ble_connected_flag || active_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_tx_val_handle == 0 || message == NULL) {
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
    if (status_json) {
        ble_manager_send_notify(status_json);
    }
}

static void send_full_status(void) {
    int bclk = 10, lrc = 11, din = 12;
    audio_get_pins(&bclk, &lrc, &din);

    int ll = 90, hr = 90, hl = 90, lr = 90;
    int oll = 0, ohr = 0, ohl = 0, olr = 0;
    if (!is_claw_mode && !is_cam_mode) {
        servo_get_angles(&ll, &hr, &hl, &lr);
        servo_get_offsets(&oll, &ohr, &ohl, &olr);
    }

    char status_str[192];
    snprintf(status_str, sizeof(status_str),
             "STATUS:angle=%d,vol=%d,bclk=%d,lrc=%d,din=%d,ll=%d,hr=%d,hl=%d,lr=%d,oll=%d,ohr=%d,ohl=%d,olr=%d\n",
             is_claw_mode ? claw_current_angle : ll,
             audio_get_volume(), bclk, lrc, din,
             ll, hr, hl, lr, oll, ohr, ohl, olr);
    ble_manager_send_notify(status_str);

    vTaskDelay(pdMS_TO_TICKS(20));

    char sens_str[192];
    snprintf(sens_str, sizeof(sens_str),
             "SENSOR:en=%d,dist=%.1f,th=%d,trip=%s,clear=%s,atrip=%s,aclear=%s\n",
             sensor_is_enabled() ? 1 : 0, (double)sensor_get_distance(), (int)sensor_get_threshold(),
             sensor_get_tripped_action(), sensor_get_cleared_action(),
             sensor_get_tripped_audio(), sensor_get_cleared_audio());
    ble_manager_send_notify(sens_str);
}

static void process_ble_command(const char* raw_cmd) {
    if (!raw_cmd) return;

    char cmd_buf[256];
    strncpy(cmd_buf, raw_cmd, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';

    char *start = cmd_buf;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (*start == '\0') return;

    char *end = start + strlen(start) - 1;
    while (end >= start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    if (*start == '\0') return;

    const char* p = start;
    ESP_LOGI(TAG, "BLE Command Received: '%s'", p);

    if (strncmp(p, "wifi:", 5) == 0 || strncmp(p, "w:", 2) == 0) {
        const char* creds = (strncmp(p, "wifi:", 5) == 0) ? (p + 5) : (p + 2);
        char* comma = strchr(creds, ',');
        if (comma) {
            char ssid_buf[33] = {0};
            char pass_buf[65] = {0};
            size_t s_len = comma - creds;
            if (s_len < sizeof(ssid_buf)) {
                strncpy(ssid_buf, creds, s_len);
                strncpy(pass_buf, comma + 1, sizeof(pass_buf) - 1);
                ESP_LOGI(TAG, "Provisioning Wi-Fi via BLE -> SSID: %s", ssid_buf);
                wifi_save_credentials(ssid_buf, pass_buf);
                wifi_manager_connect_async(ssid_buf, pass_buf);
                ble_manager_send_notify(">> Wi-Fi Saved. Connecting...\n");
                return;
            }
        }
    }

    if (strcmp(p, "sensor on") == 0 || strcmp(p, "sensor 1") == 0) {
        sensor_set_enabled(true);
        ble_manager_send_notify(">> Sensor ENABLED\n");
        send_full_status();
        return;
    }
    if (strcmp(p, "sensor off") == 0 || strcmp(p, "sensor 0") == 0) {
        sensor_set_enabled(false);
        ble_manager_send_notify(">> Sensor DISABLED\n");
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

    int leg_ang = 0;
    if (sscanf(p, "hl %d", &leg_ang) == 1) {
        servo_set_target("high_left", leg_ang);
        send_full_status();
        return;
    }
    if (sscanf(p, "hr %d", &leg_ang) == 1) {
        servo_set_target("high_right", leg_ang);
        send_full_status();
        return;
    }
    if (sscanf(p, "ll %d", &leg_ang) == 1) {
        servo_set_target("low_left", leg_ang);
        send_full_status();
        return;
    }
    if (sscanf(p, "lr %d", &leg_ang) == 1) {
        servo_set_target("low_right", leg_ang);
        send_full_status();
        return;
    }

    int c_ll = 0, c_hr = 0, c_hl = 0, c_lr = 0;
    if (sscanf(p, "cal_set offsets %d %d %d %d", &c_ll, &c_hr, &c_hl, &c_lr) == 4) {
        servo_set_calibration("offsets", c_ll, c_hr, c_hl, c_lr, true);
        ble_manager_send_notify(">> Offsets Saved to Flash\n");
        send_full_status();
        return;
    }
    if (sscanf(p, "offset ll %d", &c_ll) == 1) {
        int o_ll, o_hr, o_hl, o_lr;
        servo_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);
        servo_set_calibration("offsets", c_ll, o_hr, o_hl, o_lr, true);
        send_full_status();
        return;
    }
    if (sscanf(p, "offset hr %d", &c_hr) == 1) {
        int o_ll, o_hr, o_hl, o_lr;
        servo_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);
        servo_set_calibration("offsets", o_ll, c_hr, o_hl, o_lr, true);
        send_full_status();
        return;
    }
    if (sscanf(p, "offset hl %d", &c_hl) == 1) {
        int o_ll, o_hr, o_hl, o_lr;
        servo_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);
        servo_set_calibration("offsets", o_ll, o_hr, c_hl, o_lr, true);
        send_full_status();
        return;
    }
    if (sscanf(p, "offset lr %d", &c_lr) == 1) {
        int o_ll, o_hr, o_hl, o_lr;
        servo_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);
        servo_set_calibration("offsets", o_ll, o_hr, o_hl, c_lr, true);
        send_full_status();
        return;
    }
    if (strcmp(p, "cal_save") == 0) {
        int o_ll, o_hr, o_hl, o_lr;
        servo_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);
        servo_set_calibration("offsets", o_ll, o_hr, o_hl, o_lr, true);
        ble_manager_send_notify(">> Calibration saved to flash\n");
        send_full_status();
        return;
    }
    if (strcmp(p, "cal_get") == 0) {
        int o_ll, o_hr, o_hl, o_lr;
        servo_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);
        char reply[64];
        snprintf(reply, sizeof(reply), "CAL:oll=%d,ohr=%d,ohl=%d,olr=%d\n", o_ll, o_hr, o_hl, o_lr);
        ble_manager_send_notify(reply);
        return;
    }
    if (strcmp(p, "cal_reset") == 0) {
        servo_set_calibration("offsets", 0, 0, 0, 11, true);
        ble_manager_send_notify(">> Calibration reset to defaults\n");
        send_full_status();
        return;
    }

    if (strncmp(p, "switchkey ", 10) == 0) {
        const char *arg = p + 10;
        while (*arg == ' ') arg++;
        char *endptr = NULL;
        long btn_idx = strtol(arg, &endptr, 10);
        if (btn_idx >= 0 && btn_idx < 16 && endptr && *endptr != '\0') {
            while (*endptr == ' ' || *endptr == '\t') endptr++;
            if (*endptr != '\0') {
                nvs_handle_t handle;
                if (nvs_open("switch_cfg", NVS_READWRITE, &handle) == ESP_OK) {
                    char nvs_key[16];
                    snprintf(nvs_key, sizeof(nvs_key), "sw_%ld", btn_idx);
                    nvs_set_str(handle, nvs_key, endptr);
                    nvs_commit(handle);
                    nvs_close(handle);
                }

                char confirm[300];
                snprintf(confirm, sizeof(confirm), "SWKEY:%ld=%s\n", btn_idx, endptr);
                ble_manager_send_notify(confirm);
                ESP_LOGI(TAG, "Switch Button %ld saved to NVS: '%s'", btn_idx, endptr);
                return;
            }
        }
    }

    if (strcmp(p, "getswitchkeys") == 0) {
        nvs_handle_t handle;
        bool nvs_ok = (nvs_open("switch_cfg", NVS_READONLY, &handle) == ESP_OK);

        for (int i = 0; i < 16; i++) {
            char val[64] = {0};
            size_t len = sizeof(val);
            char nvs_key[16];
            snprintf(nvs_key, sizeof(nvs_key), "sw_%d", i);

            if (!nvs_ok || nvs_get_str(handle, nvs_key, val, &len) != ESP_OK) {
                strncpy(val, DEFAULT_SWITCH_COMMANDS[i], sizeof(val) - 1);
            }

            char line[128];
            snprintf(line, sizeof(line), "SWKEY:%d=%s\n", i, val);
            ble_manager_send_notify(line);
            vTaskDelay(pdMS_TO_TICKS(15));
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
            snprintf(line, sizeof(line), "SWKEY:%d=%s\n", i, DEFAULT_SWITCH_COMMANDS[i]);
            ble_manager_send_notify(line);
            vTaskDelay(pdMS_TO_TICKS(15));
        }
        ble_manager_send_notify(">> Switch mappings reset to defaults.\n");
        return;
    }

    if (strcmp(p, "bark") == 0 || strcmp(p, "play") == 0) {
        audio_play("dog_bark");
        ble_manager_send_notify(">> Playing Dog Bark\n");
        return;
    }
    if (strcmp(p, "beep") == 0 || strcmp(p, "test") == 0) {
        audio_play_test_beep();
        ble_manager_send_notify(">> Playing Test Beep\n");
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
        int target_vol = 0;
        if (v_str[0] == '+' || v_str[0] == '-') {
            target_vol = audio_get_volume() + atoi(v_str);
        } else {
            target_vol = atoi(v_str);
        }
        if (target_vol < 0) target_vol = 0;
        if (target_vol > 100) target_vol = 100;
        audio_set_volume(target_vol);
        char msg[64];
        snprintf(msg, sizeof(msg), ">> Volume: %d%%\n", target_vol);
        ble_manager_send_notify(msg);
        send_full_status();
        return;
    }
    if (strncmp(p, "pins ", 5) == 0) {
        int b = 0, l = 0, d = 0;
        if (sscanf(p + 5, "%d %d %d", &b, &l, &d) == 3) {
            audio_set_pins(b, l, d);
            char msg[64];
            snprintf(msg, sizeof(msg), ">> Audio pins: BCLK=%d, LRC=%d, DIN=%d\n", b, l, d);
            ble_manager_send_notify(msg);
        }
        return;
    }

    if (strcmp(p, "status") == 0) {
        send_full_status();
        return;
    }
    if (strcmp(p, "reboot") == 0) {
        ble_manager_send_notify(">> Rebooting ESP32...\n");
        vTaskDelay(pdMS_TO_TICKS(150));
        esp_restart();
        return;
    }
    if (strcmp(p, "cam_flip") == 0) {
        cam_toggle_flip();
        ble_manager_send_notify(">> Camera Flipped\n");
        return;
    }

    bool is_num = true;
    for (int i = 0; p[i] != '\0'; i++) {
        if (!isdigit((unsigned char)p[i])) { is_num = false; break; }
    }
    if (is_num || strncmp(p, "angle ", 6) == 0) {
        int ang = is_num ? atoi(p) : atoi(p + 6);
        if (is_claw_mode) {
            claw_set_angle(ang);
        } else {
            servo_set_target("all", ang);
        }
        char msg[64];
        snprintf(msg, sizeof(msg), ">> Set angle: %d\n", ang);
        ble_manager_send_notify(msg);
        send_full_status();
        return;
    }

    cJSON *json = cJSON_Parse(p);
    if (json != NULL) {
        cJSON *act_item = cJSON_GetObjectItem(json, "action");
        if (act_item && act_item->valuestring) {
            const char* act = act_item->valuestring;
            if (is_claw_mode) claw_execute_command(act);
            else servo_set_action(act);
        }

        cJSON *claw_cmd = cJSON_GetObjectItem(json, "claw_cmd");
        if (!claw_cmd) claw_cmd = cJSON_GetObjectItem(json, "cmd");
        if (claw_cmd && claw_cmd->valuestring) {
            claw_execute_command(claw_cmd->valuestring);
        }

        cJSON *claw_ang = cJSON_GetObjectItem(json, "claw_angle");
        if (!claw_ang) claw_ang = cJSON_GetObjectItem(json, "angle");
        if (claw_ang) claw_set_angle(claw_ang->valueint);

        cJSON *sen_en = cJSON_GetObjectItem(json, "sensor_enabled");
        if (sen_en) sensor_set_enabled(cJSON_IsTrue(sen_en) || sen_en->valueint != 0);

        cJSON *sen_th = cJSON_GetObjectItem(json, "sensor_threshold");
        if (sen_th) sensor_set_threshold(sen_th->valueint);

        cJSON *sen_trip = cJSON_GetObjectItem(json, "sensor_tripped_action");
        cJSON *sen_clear = cJSON_GetObjectItem(json, "sensor_cleared_action");
        if (sen_trip && sen_clear) sensor_set_actions(sen_trip->valuestring, sen_clear->valuestring);

        cJSON *sen_atrip = cJSON_GetObjectItem(json, "sensor_tripped_audio");
        cJSON *sen_aclear = cJSON_GetObjectItem(json, "sensor_cleared_audio");
        if (sen_atrip && sen_aclear) sensor_set_audio_actions(sen_atrip->valuestring, sen_aclear->valuestring);

        cJSON *cal_off = cJSON_GetObjectItem(json, "cal_offsets");
        if (cal_off) {
            cJSON *ll_o = cJSON_GetObjectItem(cal_off, "ll");
            cJSON *hr_o = cJSON_GetObjectItem(cal_off, "hr");
            cJSON *hl_o = cJSON_GetObjectItem(cal_off, "hl");
            cJSON *lr_o = cJSON_GetObjectItem(cal_off, "lr");
            if (ll_o && hr_o && hl_o && lr_o) {
                servo_set_calibration("offsets", ll_o->valueint, hr_o->valueint, hl_o->valueint, lr_o->valueint, true);
            }
        }

        cJSON_Delete(json);
        ble_manager_send_notify(">> JSON Handled\n");
        send_full_status();
        return;
    }

    if (is_claw_mode) {
        claw_execute_command(p);
    } else {
        servo_set_action(p);
    }

    char reply[300];
    snprintf(reply, sizeof(reply), ">> Action: %s\n", p);
    ble_manager_send_notify(reply);
}

static int gatt_svr_chr_access_rx(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        char buf[512];
        if (om_len >= sizeof(buf)) {
            om_len = sizeof(buf) - 1;
        }

        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, NULL);
        if (rc == 0) {
            buf[om_len] = '\0';
            char *line = strtok(buf, "\r\n");
            while (line != NULL) {
                process_ble_command(line);
                line = strtok(NULL, "\r\n");
            }
        }
    }
    return 0;
}

static int gatt_svr_chr_access_tx(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg) {
    return 0;
}

static const struct ble_gatt_chr_def gatt_svr_chrs[] = {
    {
        .uuid = &gatt_svr_chr_rx_uuid.u,
        .access_cb = gatt_svr_chr_access_rx,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = NULL,
        .cpfd = NULL,
    },
    {
        .uuid = &gatt_svr_chr_tx_uuid.u,
        .access_cb = gatt_svr_chr_access_tx,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &s_tx_val_handle,
        .cpfd = NULL,
    },
    {}
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uart_uuid.u,
        .includes = NULL,
        .characteristics = gatt_svr_chrs,
    },
    {}
};

static void ble_advertise(void);

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                active_conn_handle = event->connect.conn_handle;
                is_ble_connected_flag = true;
                ESP_LOGI(TAG, "BLE Client Connected! Connection Handle: %d", active_conn_handle);
                send_full_status();
            } else {
                active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                is_ble_connected_flag = false;
                ble_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "BLE Client Disconnected! Reason: %d", event->disconnect.reason);
            active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            is_ble_connected_flag = false;
            ble_advertise();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_advertise();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "BLE Client Subscribed to Notifications");
            send_full_status();
            break;

        default:
            break;
    }
    return 0;
}

static void ble_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting adv fields: %d", rc);
        return;
    }

    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = (ble_uuid128_t *)&gatt_svr_svc_uart_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    ble_gap_adv_rsp_set_fields(&rsp_fields);

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Advertising BLE as '%s'...", DEVICE_NAME);
    } else {
        ESP_LOGE(TAG, "Error starting advertising: %d", rc);
    }
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void) {
    ESP_LOGI(TAG, "NimBLE host synchronized.");
    ble_advertise();
}

static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "NimBLE host reset: reason=%d", reason);
}

void ble_manager_init(void) {
    nimble_port_init();

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return;
    }

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE Manager Initialized with Nordic UART Service.");
}