// main\wifi_manager.cpp
#include "wifi_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <arpa/inet.h>
#include "mdns.h"
#include "ble_manager.h"

static const char *TAG = "WIFI_MGR";

static TaskHandle_t dns_task_handle = NULL;
static TaskHandle_t udp_task_handle = NULL;

static void dns_server_task(void *pvParameters) {
    char rx_buffer[128];
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);
    
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE("DNS", "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    
    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE("DNS", "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI("DNS", "DNS Captive Portal active on port 53");

    while (1) {
        struct sockaddr_storage source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
        
        if (len < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (len > 12) {
            rx_buffer[2] = 0x81; 
            rx_buffer[3] = 0x80;
            rx_buffer[6] = rx_buffer[4]; 
            rx_buffer[7] = rx_buffer[5];
            rx_buffer[8] = 0; rx_buffer[9] = 0; 
            rx_buffer[10] = 0; rx_buffer[11] = 0; 
            
            int pos = len;
            rx_buffer[pos++] = 0xC0; rx_buffer[pos++] = 0x0C;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x01;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x01;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x00;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x3C;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x04;
            rx_buffer[pos++] = 192; rx_buffer[pos++] = 168;
            rx_buffer[pos++] = 4; rx_buffer[pos++] = 1;
            
            sendto(sock, rx_buffer, pos, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
    }
}

static void udp_broadcast_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(4210);
    dest_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    
    while (1) {
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "ROBOT_DOG_IP:" IPSTR, IP2STR(&ip_info.ip));
            sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "Wi-Fi Connected! IP: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "===============================================");
        
        char ip_str[32];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ble_manager_notify_ip(ip_str);

        mdns_init();
        mdns_hostname_set("robotdog");
        mdns_instance_name_set("ESP32 Robot");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

        if (udp_task_handle == NULL) {
            xTaskCreate(udp_broadcast_task, "udp_bcast", 2048, NULL, 5, &udp_task_handle);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Station disconnected from external AP. SoftAP remains active on 192.168.4.1.");
    }
}

void wifi_manager_connect_async(const char* ssid, const char* pass) {
    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, ssid, 32);
    strncpy((char*)sta_config.sta.password, pass, 64);
    sta_config.sta.listen_interval = 3;
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;
    
    ESP_LOGI(TAG, "Connecting to SSID: %s (SoftAP remains always active)", ssid);
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_connect();
}

void wifi_manager_init() {
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    esp_event_handler_instance_t instance_sta_disc;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, NULL, &instance_sta_disc));

    // Configure Rock-Solid Standalone SoftAP on 192.168.4.1
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
        IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);
    }

    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, "ESPRobot_Config");
    strcpy((char*)ap_config.ap.password, "12345678");
    ap_config.ap.ssid_len = strlen("ESPRobot_Config");
    ap_config.ap.channel = 6; // Move to Ch 6 to avoid interference
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.beacon_interval = 100;
    ap_config.ap.ssid_hidden = 0;
    
    // Disable PMF to guarantee visibility on older Apple/Android devices
    ap_config.ap.pmf_cfg.capable = false;
    ap_config.ap.pmf_cfg.required = false;

    // Check for saved station credentials
    char saved_ssid[33] = {0};
    char saved_pass[65] = {0};
    uint8_t force_ap = 0;
    bool has_saved_creds = false;

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t s_len = sizeof(saved_ssid);
        size_t p_len = sizeof(saved_pass);
        if (nvs_get_str(my_handle, "wifi_ssid", saved_ssid, &s_len) == ESP_OK && strlen(saved_ssid) > 0) {
            nvs_get_str(my_handle, "wifi_pass", saved_pass, &p_len);
            nvs_get_u8(my_handle, "force_ap", &force_ap);
            if (force_ap == 0) {
                has_saved_creds = true;
            }
        }
        nvs_close(my_handle);
    }

    if (has_saved_creds) {
        ESP_LOGI(TAG, "Starting dual AP+STA Mode (SoftAP 'ESPRobot_Config' + STA connecting to '%s')", saved_ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

        wifi_config_t sta_config = {};
        strncpy((char*)sta_config.sta.ssid, saved_ssid, 32);
        strncpy((char*)sta_config.sta.password, saved_pass, 64);
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_config.sta.pmf_cfg.capable = true;
        sta_config.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

        // Force 20MHz bandwidth for maximum phone compatibility
        esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

        ESP_ERROR_CHECK(esp_wifi_start());
        
        // CRITICAL FIX: Disable Modem Sleep. Guarantees AP beacons never drop on battery.
        esp_wifi_set_ps(WIFI_PS_NONE);
        
        esp_wifi_connect();
    } else {
        ESP_LOGI(TAG, "Starting pure SoftAP Mode (SSID 'ESPRobot_Config' on 192.168.4.1)");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        
        // Force 20MHz bandwidth for maximum phone compatibility
        esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
        
        ESP_ERROR_CHECK(esp_wifi_start());
        
        // CRITICAL FIX: Disable Modem Sleep. Guarantees AP beacons never drop on battery.
        esp_wifi_set_ps(WIFI_PS_NONE);
    }

    // Set stable TX power: 68 = 17 dBm (Stronger signal for battery without USB ground plane)
    int8_t tx_power = 68;
    nvs_handle_t pwr_handle;
    if (nvs_open("storage", NVS_READONLY, &pwr_handle) == ESP_OK) {
        nvs_get_i8(pwr_handle, "wifi_tx_pwr", &tx_power);
        nvs_close(pwr_handle);
    }
    if (tx_power < 40) tx_power = 40;
    if (tx_power > 78) tx_power = 78;
    esp_wifi_set_max_tx_power(tx_power);
    ESP_LOGI(TAG, "Wi-Fi Running: SSID 'ESPRobot_Config' (Channel 6, TX %d)", tx_power);

    ble_manager_notify_ip("192.168.4.1");
    if (dns_task_handle == NULL) {
        xTaskCreate(dns_server_task, "dns_task", 4096, NULL, 5, &dns_task_handle);
    }
}

char* wifi_scan_networks_json() {
    esp_wifi_scan_stop();
    
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = true;
    
    cJSON *root = cJSON_CreateArray();
    esp_err_t scan_err = esp_wifi_scan_start(&scan_config, true);
    
    if (scan_err == ESP_OK) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        
        if (ap_count > 0) {
            if (ap_count > 30) ap_count = 30;
            wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (ap_info != NULL) {
                if (esp_wifi_scan_get_ap_records(&ap_count, ap_info) == ESP_OK) {
                    for(int i = 0; i < ap_count; i++) {
                        if (strlen((char*)ap_info[i].ssid) > 0) {
                            cJSON_AddItemToArray(root, cJSON_CreateString((char*)ap_info[i].ssid));
                        }
                    }
                }
                free(ap_info);
            }
        }
    } else {
        ESP_LOGE(TAG, "Wi-Fi Scan failed: %s", esp_err_to_name(scan_err));
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); 
    
    return json_str;
}

void wifi_save_credentials(const char* ssid, const char* pass) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_str(my_handle, "wifi_ssid", ssid);
        nvs_set_str(my_handle, "wifi_pass", pass);
        nvs_set_u8(my_handle, "force_ap", 0);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Saved SSID: %s", ssid);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS to save credentials!");
    }
}

void wifi_manager_force_ap_temporary() {
    ESP_LOGI(TAG, "Forcing pure AP Mode ON temporarily.");
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_AP);
    ble_manager_notify_ip("192.168.4.1");
    
    if (dns_task_handle == NULL) {
        xTaskCreate(dns_server_task, "dns_task", 4096, NULL, 5, &dns_task_handle);
    }
}

void wifi_set_tx_power(int8_t power) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_i8(my_handle, "wifi_tx_pwr", power);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    esp_wifi_set_max_tx_power(power);
    ESP_LOGI(TAG, "Wi-Fi TX Power set to %d", power);
}