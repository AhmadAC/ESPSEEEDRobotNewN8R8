// main/ble_manager.cpp
#include "ble_manager.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "servo_controller.h"
#include "claw_controller.h"
#include "wifi_manager.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "BLE_MGR";

extern bool is_claw_mode;

static const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(0xABF0);
static const ble_uuid16_t uart_rx_uuid = BLE_UUID16_INIT(0xABF1);
static const ble_uuid16_t wifi_creds_uuid = BLE_UUID16_INIT(0xABF2);
static const ble_uuid16_t ip_addr_uuid = BLE_UUID16_INIT(0xABF3);

uint16_t ip_chr_val_handle = 0;
uint16_t active_conn_handle = 0;
char current_ip[32] = "0.0.0.0";
static bool is_ble_connected_flag = false;

bool ble_manager_is_connected() {
    return is_ble_connected_flag;
}

static void process_ble_command(const char* cmd) {
    if (!cmd || strlen(cmd) == 0) return;

    ESP_LOGI(TAG, "BLE Command Received: %s", cmd);

    // Strip "action:" prefix if present
    if (strncmp(cmd, "action:", 7) == 0) {
        cmd += 7;
    }

    // 1. Try parsing JSON payload
    cJSON *json = cJSON_Parse(cmd);
    if (json != NULL) {
        // Handle Wi-Fi Provisioning via JSON
        cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
        cJSON *pass_item = cJSON_GetObjectItem(json, "pass");
        if (!pass_item) pass_item = cJSON_GetObjectItem(json, "password");

        if (ssid_item && ssid_item->valuestring && pass_item && pass_item->valuestring) {
            ESP_LOGI(TAG, "Wi-Fi Credentials received via BLE JSON - SSID: %s", ssid_item->valuestring);
            wifi_save_credentials(ssid_item->valuestring, pass_item->valuestring);
            wifi_manager_connect_async(ssid_item->valuestring, pass_item->valuestring);
            cJSON_Delete(json);
            return;
        }

        // Handle Claw commands via JSON
        cJSON *claw_cmd = cJSON_GetObjectItem(json, "claw_cmd");
        if (!claw_cmd) claw_cmd = cJSON_GetObjectItem(json, "cmd");
        if (claw_cmd && claw_cmd->valuestring) {
            claw_execute_command(claw_cmd->valuestring);
        }

        cJSON *claw_angle = cJSON_GetObjectItem(json, "claw_angle");
        if (!claw_angle) claw_angle = cJSON_GetObjectItem(json, "angle");
        if (claw_angle) {
            claw_set_angle(claw_angle->valueint);
        }

        // Handle Robot Actions via JSON
        cJSON *act_item = cJSON_GetObjectItem(json, "action");
        if (act_item && act_item->valuestring) {
            if (is_claw_mode) {
                claw_execute_command(act_item->valuestring);
            } else {
                servo_set_action(act_item->valuestring);
            }
        }

        // Handle Individual Servos via JSON
        cJSON *ll = cJSON_GetObjectItem(json, "ll");
        cJSON *lr = cJSON_GetObjectItem(json, "lr");
        cJSON *hl = cJSON_GetObjectItem(json, "hl");
        cJSON *hr = cJSON_GetObjectItem(json, "hr");

        if (ll || lr || hl || hr) {
            if (ll) servo_set_target_silent("low_left", ll->valueint);
            if (lr) servo_set_target_silent("low_right", lr->valueint);
            if (hl) servo_set_target_silent("high_left", hl->valueint);
            if (hr) servo_set_target_silent("high_right", hr->valueint);
            servo_apply_targets();
        }

        cJSON_Delete(json);
        return;
    }

    // 2. Fallback to Plain Text / CSV Commands
    if (strncmp(cmd, "claw:", 5) == 0) {
        claw_execute_command(cmd + 5);
    } else if (strncmp(cmd, "claw_angle:", 11) == 0) {
        claw_set_angle(atoi(cmd + 11));
    } else if (is_claw_mode) {
        if (strcmp(cmd, "open") == 0 || strcmp(cmd, "close") == 0 ||
            strcmp(cmd, "half_open") == 0 || strcmp(cmd, "half_close") == 0) {
            claw_execute_command(cmd);
        } else if (cmd[0] >= '0' && cmd[0] <= '9') {
            claw_set_angle(atoi(cmd));
        } else {
            claw_execute_command(cmd);
        }
    } else {
        servo_set_action(cmd);
    }
}

static int ble_rx_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char buf[256] = {0};
    int len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > 0 && len < sizeof(buf)) {
        os_mbuf_copydata(ctxt->om, 0, len, buf);
        for(int i = 0; i < len; i++) {
            if(buf[i] == '\r' || buf[i] == '\n') buf[i] = '\0';
        }
        process_ble_command(buf);
    }
    return 0;
}

static int ble_wifi_rx_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char buf[256] = {0};
    int len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > 0 && len < sizeof(buf)) {
        os_mbuf_copydata(ctxt->om, 0, len, buf);
        char* comma = strchr(buf, ',');
        if (comma) {
            *comma = '\0';
            ESP_LOGI(TAG, "Provisioning via BLE - SSID: %s", buf);
            wifi_save_credentials(buf, comma + 1);
            wifi_manager_connect_async(buf, comma + 1);
        }
    }
    return 0;
}

static int ble_ip_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    os_mbuf_append(ctxt->om, current_ip, strlen(current_ip));
    return 0;
}

static const struct ble_gatt_chr_def gatt_chrs[] = {
    {
        .uuid = (const ble_uuid_t *)&uart_rx_uuid,
        .access_cb = ble_rx_cb,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr
    },
    {
        .uuid = (const ble_uuid_t *)&wifi_creds_uuid,
        .access_cb = ble_wifi_rx_cb,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE,
        .min_key_size = 0,
        .val_handle = nullptr
    },
    {
        .uuid = (const ble_uuid_t *)&ip_addr_uuid,
        .access_cb = ble_ip_cb,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &ip_chr_val_handle
    },
    {}
};

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (const ble_uuid_t *)&svc_uuid,
        .includes = nullptr,
        .characteristics = gatt_chrs
    },
    {}
};

static void ble_app_on_sync(void);

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                active_conn_handle = event->connect.conn_handle;
                is_ble_connected_flag = true;
                ESP_LOGI(TAG, "BLE Client Connected!");
                wifi_manager_force_ap_temporary();
            } else {
                ESP_LOGE(TAG, "BLE Connection failed! Status: %d", event->connect.status);
                is_ble_connected_flag = false;
                ble_app_on_sync();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "BLE Client Disconnected! Reason: %d", event->disconnect.reason);
            active_conn_handle = 0;
            is_ble_connected_flag = false;
            ble_app_on_sync();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "BLE Advertising completed. Restarting...");
            ble_app_on_sync();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "BLE Client Subscribed to Characteristic");
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "BLE MTU Updated to %d bytes", event->mtu.value);
            break;

        case BLE_GAP_EVENT_CONN_UPDATE:
            ESP_LOGI(TAG, "BLE Connection Parameters Updated");
            break;
    }
    return 0;
}

static void ble_app_on_sync(void) {
    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"ESPRobot";
    fields.name_len = 8;
    fields.name_is_complete = 1;

    ble_uuid16_t adv_uuids[] = { svc_uuid };
    fields.uuids16 = adv_uuids;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    ESP_LOGI(TAG, "BLE UART/Provisioning Advertising Started");
}

void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_manager_notify_ip(const char* ip) {
    strncpy(current_ip, ip, sizeof(current_ip)-1);

    if (active_conn_handle != 0 && ip_chr_val_handle != 0) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(current_ip, strlen(current_ip));
        ble_gatts_notify_custom(active_conn_handle, ip_chr_val_handle, om);
        ESP_LOGI(TAG, "Sent new IP Address '%s' over BLE to connected Client", current_ip);
    }
}

void ble_manager_send_status(const char* status_json) {
    if (active_conn_handle != 0 && ip_chr_val_handle != 0 && status_json) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(status_json, strlen(status_json));
        ble_gatts_notify_custom(active_conn_handle, ip_chr_val_handle, om);
    }
}

void ble_manager_init() {
    nimble_port_init();

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_device_name_set("ESPRobot");
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE Manager Initialization Finished.");
}