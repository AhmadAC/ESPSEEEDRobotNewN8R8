// main\cam_controller.cpp
#include "cam_controller.h"
#include "claw_controller.h"
#include "esp_camera.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_psram.h"
#include <string.h>

static const char *TAG = "CAM_CTRL";

extern bool is_claw_mode;

// Pins isolated away from audio (D10=10, D11=11, D12=12), servos (1,2,3,4), and sensor (5,6)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     -1
#define SIOD_GPIO_NUM     -1
#define SIOC_GPIO_NUM     -1
#define Y9_GPIO_NUM       -1
#define Y8_GPIO_NUM       -1
#define Y7_GPIO_NUM       -1
#define Y6_GPIO_NUM       -1
#define Y5_GPIO_NUM       -1
#define Y4_GPIO_NUM       -1
#define Y3_GPIO_NUM       -1
#define Y2_GPIO_NUM       -1
#define VSYNC_GPIO_NUM    -1
#define HREF_GPIO_NUM     -1
#define PCLK_GPIO_NUM     -1

volatile bool isCapturing = false;
static volatile bool isConnected = false;
static volatile bool captureRequested = false;
static volatile bool is_streaming = false;
static bool espnow_initialized = false;
static bool cam_flipped = true;
static bool cam_initialized = false;

static uint8_t pyControllerMac[6];
static uint8_t cam_mac[6];

bool cam_is_available() {
    return cam_initialized;
}

void cam_toggle_flip() {
    if (!cam_initialized) return;
    cam_flipped = !cam_flipped;
    sensor_t * s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, cam_flipped ? 1 : 0);
        s->set_hmirror(s, cam_flipped ? 1 : 0);
    }
}

static void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    const uint8_t *mac = info->src_addr;
    
    if (len >= 14 && (strncmp((const char*)incomingData, "pyCAR_DISCOVER", 14) == 0 || strncmp((const char*)incomingData, "pyCAM_DISCOVER", 14) == 0)) {
        if (!isConnected || memcmp(pyControllerMac, mac, 6) != 0) {
            ESP_LOGI(TAG, "Received 'DISCOVER' via ESP-NOW! Registering Controller.");
            
            if (isConnected && esp_now_is_peer_exist(pyControllerMac)) {
                esp_now_del_peer(pyControllerMac);
            }
            
            memcpy(pyControllerMac, mac, 6);
            
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, pyControllerMac, 6);
            peerInfo.channel = 1; 
            peerInfo.ifidx = WIFI_IF_STA;
            peerInfo.encrypt = false;
            
            if (!esp_now_is_peer_exist(pyControllerMac)) {
                esp_now_add_peer(&peerInfo);
            }
            isConnected = true;
        }
        
        const char* ackMsg = "pyCAM_ACK";
        esp_now_send(pyControllerMac, (uint8_t *)ackMsg, strlen(ackMsg));
    } 
    else if (len >= 9 && strncmp((const char*)incomingData, "pyCAM_REQ", 9) == 0) {
        captureRequested = true;
    }
    else if (len >= 11 && strncmp((const char*)incomingData, "pyCAM_STR_1", 11) == 0) {
        is_streaming = true;
    }
    else if (len >= 11 && strncmp((const char*)incomingData, "pyCAM_STR_0", 11) == 0) {
        is_streaming = false;
    }
    else if (len == 6 && incomingData[0] == 67) {
        if (is_claw_mode) {
            uint8_t btns = incomingData[5];
            uint8_t dpad = btns & 0x0F;
            static uint8_t last_dpad = 8;
            
            if (dpad != last_dpad) {
                bool is_right = (dpad == 2 || dpad == 1 || dpad == 3);
                bool was_right = (last_dpad == 2 || last_dpad == 1 || last_dpad == 3);
                
                if (is_right && !was_right) {
                    if (claw_current_angle > 90) {
                        claw_execute_command("close");
                    } else {
                        claw_execute_command("open");
                    }
                }
                last_dpad = dpad;
            }
        }
    }
}

static void send_raw_image(camera_fb_t *fb) {
    if (!fb) return;
    uint8_t raw_packet[1400];
    static uint16_t seq_num = 0;
    
    raw_packet[0] = 0x08; raw_packet[1] = 0x00;
    raw_packet[2] = 0x00; raw_packet[3] = 0x00;
    memcpy(&raw_packet[4], pyControllerMac, 6);  
    memcpy(&raw_packet[10], cam_mac, 6);         
    memcpy(&raw_packet[16], pyControllerMac, 6); 

    raw_packet[24] = 'C'; raw_packet[25] = 'A'; raw_packet[26] = 'M';
    
    int max_payload = 1300; 
    uint16_t total_chunks = (fb->len + max_payload - 1) / max_payload;
    
    for (uint16_t i = 0; i < total_chunks; i++) {
        uint16_t seq_ctrl = (seq_num++) << 4; 
        raw_packet[22] = seq_ctrl & 0xFF;
        raw_packet[23] = (seq_ctrl >> 8) & 0xFF;

        memcpy(&raw_packet[27], &i, 2);
        memcpy(&raw_packet[29], &total_chunks, 2);
        
        int offset = i * max_payload;
        uint16_t len = fb->len - offset;
        if (len > max_payload) len = max_payload;
        
        memcpy(&raw_packet[31], &len, 2);
        memcpy(&raw_packet[33], fb->buf + offset, len);
        
        esp_wifi_80211_tx(WIFI_IF_STA, raw_packet, 33 + len, false);
        esp_rom_delay_us(2000); 
    }
}

static void cam_espnow_task(void *pvParameters) {
    while (1) {
        if (isConnected && cam_initialized) {
            if (captureRequested) {
                captureRequested = false;
                sensor_t * s = esp_camera_sensor_get();
                if (s != NULL) s->set_quality(s, 6); 
                vTaskDelay(pdMS_TO_TICKS(50)); 
                
                camera_fb_t *fb = esp_camera_fb_get();
                if (fb) {
                    send_raw_image(fb);
                    esp_camera_fb_return(fb);
                }
                if (s != NULL) s->set_quality(s, 14); 
            } else if (is_streaming && !isCapturing) {
                camera_fb_t *fb = esp_camera_fb_get();
                if (fb) {
                    send_raw_image(fb);
                    esp_camera_fb_return(fb);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void cam_controller_init() {
    // Camera is disabled by default on ESP32-S3-Zero N8R8 to prevent pin contention with audio
    cam_initialized = false;
    ESP_LOGI(TAG, "Camera module disabled by default on ESP32-S3-Zero N8R8 (GPIO 10, 11, 12 reserved for I2S audio).");
}

void cam_espnow_init() {
    if (espnow_initialized) return;
    
    esp_wifi_get_mac(WIFI_IF_STA, cam_mac);
    
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);
    ESP_LOGI(TAG, "ESP-NOW Subsystem initialized.");
    
    xTaskCreatePinnedToCore(cam_espnow_task, "ESPNOW_Task", 4096, NULL, 3, NULL, 1);
    espnow_initialized = true;
}

void cam_espnow_pair_claw() {
    if (!espnow_initialized) {
        cam_espnow_init();
    }
    
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 1;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, broadcast_mac, 6);
    
    if (!esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_add_peer(&peerInfo);
    }
    
    const char* ackMsg = "pyCAR_ACK";
    esp_now_send(broadcast_mac, (const uint8_t *)ackMsg, strlen(ackMsg));
    
    const char* camAckMsg = "pyCAM_ACK";
    esp_now_send(broadcast_mac, (const uint8_t *)camAckMsg, strlen(camAckMsg));
    
    ESP_LOGI(TAG, "Broadcasted ESP-NOW Pair Request to PyController");
}