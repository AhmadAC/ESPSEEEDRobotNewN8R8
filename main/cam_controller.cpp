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

// ----------------------------------------------------
// Seeed Studio XIAO ESP32S3 Sense OV2640 Pinout
// ----------------------------------------------------
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

volatile bool isCapturing = false;
static volatile bool isConnected = false;
static volatile bool captureRequested = false;
static volatile bool is_streaming = false;
static bool espnow_initialized = false;
static bool cam_flipped = true;

static uint8_t pyControllerMac[6];
static uint8_t cam_mac[6];

// ----------------------------------------------------
// Camera Video Orientation Flip Routine
// ----------------------------------------------------
void cam_toggle_flip() {
    cam_flipped = !cam_flipped;
    sensor_t * s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, cam_flipped ? 1 : 0);
        s->set_hmirror(s, cam_flipped ? 1 : 0);
    }
}

// ----------------------------------------------------
// ESP-NOW Receive Callback (ESP-IDF v5 Signature)
// ----------------------------------------------------
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
    // Listen for PyController Gamepad Controller payloads
    else if (len == 6 && incomingData[0] == 67) {
        if (is_claw_mode) {
            uint8_t btns = incomingData[5];
            uint8_t dpad = btns & 0x0F; // Mask out the A/B/X/Y face buttons
            static uint8_t last_dpad = 8;
            
            if (dpad != last_dpad) {
                // Check if the Right D-Pad (2=Right, 1=Up-Right, 3=Down-Right) was just pressed
                bool is_right = (dpad == 2 || dpad == 1 || dpad == 3);
                bool was_right = (last_dpad == 2 || last_dpad == 1 || last_dpad == 3);
                
                if (is_right && !was_right) {
                    // Toggle Claw open/close state based on its current position
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

// ----------------------------------------------------
// Raw Packet Transmitter for ESP-NOW Stream
// ----------------------------------------------------
static void send_raw_image(camera_fb_t *fb) {
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

// ----------------------------------------------------
// FreeRTOS Task: ESP-NOW Loop
// ----------------------------------------------------
static void cam_espnow_task(void *pvParameters) {
    while (1) {
        if (isConnected) {
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

// ----------------------------------------------------
// FreeRTOS Task: Dedicated Port 81 TCP Stream
// ----------------------------------------------------
static void cam_stream_task(void *pvParameters) {
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(81);

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create TCP socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 3) != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Camera Raw TCP Stream Server listening on Port 81");

    while (1) {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Set short timeout to flush incoming HTTP GET request
        struct timeval timeout;
        timeout.tv_sec = 1; timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        char rx_buffer[128];
        recv(sock, rx_buffer, sizeof(rx_buffer), 0); 
        
        // Disable timeout for the endless streaming loop
        timeout.tv_sec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        const char* HEADER = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=123456789000000000000987654321\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
        send(sock, HEADER, strlen(HEADER), 0);

        const char* BOUNDARY = "\r\n--123456789000000000000987654321\r\n";
        const char* CTYPE = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

        ESP_LOGI(TAG, "Stream client connected to Port 81!");

        while (1) {
            if (isCapturing) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            camera_fb_t * fb = esp_camera_fb_get();
            if (!fb) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            if (send(sock, BOUNDARY, strlen(BOUNDARY), 0) < 0) { esp_camera_fb_return(fb); break; }
            
            char buf[128];
            int len = sprintf(buf, CTYPE, fb->len);
            if (send(sock, buf, len, 0) < 0) { esp_camera_fb_return(fb); break; }

            int sent = send(sock, fb->buf, fb->len, 0);
            esp_camera_fb_return(fb);

            if (sent < 0) break; // Client disconnected

            vTaskDelay(pdMS_TO_TICKS(60)); // Stream pacing
        }
        ESP_LOGI(TAG, "Stream client disconnected.");
        close(sock);
    }
}

void cam_controller_init() {
    camera_config_t config;
    
    // Repinned internal LEDC mapping to avoid conflicting with the 50Hz Servos on Timer 0
    config.ledc_channel = LEDC_CHANNEL_5;
    config.ledc_timer   = LEDC_TIMER_2;
    
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    
    // Crucial: 20MHz is the optimal clock frequency for OV2640 standard operational synchronization
    config.xclk_freq_hz = 20000000; 
    config.pixel_format = PIXFORMAT_JPEG;
    
    // CRITICAL FIX: Changing grab mode to CAMERA_GRAB_WHEN_EMPTY completely halts mid-frame pointer 
    // switching inside the DMA channel, successfully eliminating "NO-SOI" packet truncation
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY; 

    if (esp_psram_is_initialized()) {
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.frame_size = FRAMESIZE_UXGA; 
        config.jpeg_quality = 12; // Stable quality floor to ensure high-res JPEG fits entirely within buffer limits
        config.fb_count = 2;
        ESP_LOGI(TAG, "PSRAM found. High resolution UXGA enabled.");
    } else {
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        ESP_LOGW(TAG, "WARNING: PSRAM NOT FOUND! Falling back to low resolution SVGA.");
    }

    if (esp_camera_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return;
    }
    ESP_LOGI(TAG, "Camera hardware initialized!");

    sensor_t * s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, cam_flipped ? 1 : 0);   
        s->set_hmirror(s, cam_flipped ? 1 : 0); 
        s->set_framesize(s, FRAMESIZE_QVGA); 
    }

    xTaskCreatePinnedToCore(cam_stream_task, "StreamTask", 4096, NULL, 4, NULL, 1);
}

void cam_espnow_init() {
    if (espnow_initialized) return;
    
    esp_wifi_get_mac(WIFI_IF_STA, cam_mac);
    
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);
    ESP_LOGI(TAG, "ESP-NOW Subsystem initialized. Waiting for PyController broadcasts...");
    
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