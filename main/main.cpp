// main/main.cpp
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

// Modular Component Headers
#include "wifi_manager.h"
#include "sensor_monitor.h"
#include "servo_controller.h"
#include "claw_controller.h"
#include "cam_controller.h"
#include "web_server.h"
#include "ble_manager.h"
#include "audio_player.h"

// Failsafe forward declaration to guarantee compilation
extern void web_server_init();

static const char *TAG = "MAIN";

bool is_claw_mode = false; 
bool is_cam_mode = false; 

// FreeRTOS task to monitor standard input for REPL control characters
static void console_read_task(void *pvParameter) {
    ESP_LOGI("REPL", "==================================================");
    ESP_LOGI("REPL", "Available Device REPL Commands:");
    ESP_LOGI("REPL", "  mode claw  - Switch to Claw Controller Profile");
    ESP_LOGI("REPL", "  mode robot - Switch to ESPRobot Profile");
    ESP_LOGI("REPL", "  mode cam   - Switch to Raw Camera Profile");
    ESP_LOGI("REPL", "  ap         - Force AP Mode (Wi-Fi Hotspot)");
    ESP_LOGI("REPL", "  wifi       - Connect to saved Wi-Fi");
    ESP_LOGI("REPL", "  bt         - Switch to Bluetooth Control");
    ESP_LOGI("REPL", "  wifi_power <val> - Set Wi-Fi TX power (8=2dBm, 52=13dBm, 84=21dBm)");
    ESP_LOGI("REPL", "  reset      - Factory Reset NVS (Restores Settings)");
    ESP_LOGI("REPL", "  sound      - Enable Audio UI");
    ESP_LOGI("REPL", "  no sound   - Disable & Hide Audio UI");
    ESP_LOGI("REPL", "  connect    - Pair with PyController via ESP-NOW");
    ESP_LOGI("REPL", "  (Claw Mode)  0-180 / open / close / half");
    ESP_LOGI("REPL", "  (Robot Mode) sit, stand, forward, backward, stop...");
    ESP_LOGI("REPL", "  (Robot Mode) ll/lr/hl/hr <angle> - e.g. 'll 90'");
    ESP_LOGI("REPL", "  (Robot Mode) sync on / sync off - Link pair legs");
    ESP_LOGI("REPL", "  (Robot Mode) all <angle> - Set all legs to angle");
    ESP_LOGI("REPL", "  (Robot Mode) sensor on / sensor off");
    ESP_LOGI("REPL", "  (Robot Mode) play <sound_name> - e.g. 'play dog_bark' or 'play record_3s'");
    ESP_LOGI("REPL", "  (Ctrl+C)   - Stop Motors / Relax Claw");
    ESP_LOGI("REPL", "  (Ctrl+D)   - Soft Reboot");
    ESP_LOGI("REPL", "==================================================");
    
    int fd = fileno(stdin);
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    uint8_t buf[64];
    char cmd[64];
    int cmd_idx = 0;
    static bool repl_sync = false;
    
    while (1) {
        int len = read(fd, buf, sizeof(buf));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                if (buf[i] == 0x03) { 
                    ESP_LOGW("REPL", "[Sent Ctrl+C - Interrupt]");
                    if (is_claw_mode) claw_execute_command("stop");
                    else if (!is_cam_mode) servo_set_action("stop");
                } else if (buf[i] == 0x04) { 
                    ESP_LOGW("REPL", "[Sent Ctrl+D - Soft Reboot]");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();
                } else if (buf[i] == '\r' || buf[i] == '\n') {
                    if (cmd_idx > 0) {
                        cmd[cmd_idx] = '\0';
                        if (strcmp(cmd, "reset") == 0) {
                            ESP_LOGW("REPL", "Command 'reset' received. Factory Resetting NVS...");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_erase_all(h);
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strcmp(cmd, "mode claw") == 0) {
                            ESP_LOGW("REPL", "Command 'mode claw' received. Switching profile...");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_str(h, "dev_mode", "claw");
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strcmp(cmd, "mode robot") == 0) {
                            ESP_LOGW("REPL", "Command 'mode robot' received. Switching profile...");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_str(h, "dev_mode", "robot");
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strcmp(cmd, "mode cam") == 0) {
                            ESP_LOGW("REPL", "Command 'mode cam' received. Switching profile...");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_str(h, "dev_mode", "cam");
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strcmp(cmd, "bt") == 0) {
                            ESP_LOGW("REPL", "Command 'bt' received. Switching to Bluetooth Mode...");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_str(h, "boot_mode", "bt");
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strcmp(cmd, "wifi") == 0) {
                            ESP_LOGW("REPL", "Command 'wifi' received. Switching to Wi-Fi Mode...");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_str(h, "boot_mode", "wifi");
                                nvs_set_u8(h, "force_ap", 0); 
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strcmp(cmd, "ap") == 0) {
                            ESP_LOGW("REPL", "Command 'ap' received. Switching to Forced AP Mode...");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_str(h, "boot_mode", "wifi"); 
                                nvs_set_u8(h, "force_ap", 1);        
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strcmp(cmd, "sound") == 0) {
                            ESP_LOGW("REPL", "Command 'sound' received. Audio UI Enabled.");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_u8(h, "sound_en", 1);
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strcmp(cmd, "no sound") == 0) {
                            ESP_LOGW("REPL", "Command 'no sound' received. Audio UI Disabled.");
                            nvs_handle_t h;
                            if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_u8(h, "sound_en", 0);
                                nvs_commit(h);
                                nvs_close(h);
                            }
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_restart();
                        } else if (strncmp(cmd, "wifi_power ", 11) == 0) {
                            int pwr = atoi(cmd + 11);
                            if (pwr < 8) pwr = 8;
                            if (pwr > 84) pwr = 84;
                            wifi_set_tx_power((int8_t)pwr);
                            ESP_LOGW("REPL", "Command 'wifi_power' received. TX Power set to %d.", pwr);
                        } else if (strcmp(cmd, "connect") == 0) {
                            ESP_LOGW("REPL", "Command 'connect' received. Initiating ESP-NOW pairing...");
                            if (is_claw_mode) {
                                cam_espnow_pair_claw();
                            } else {
                                ESP_LOGW("REPL", "ESP-NOW pairing via REPL is configured for Claw/Cam Mode.");
                            }
                        } else if (is_claw_mode) {
                            bool is_numeric = true;
                            int cmd_len = strlen(cmd);
                            for (int j = 0; j < cmd_len; j++) {
                                if (cmd[j] < '0' || cmd[j] > '9') {
                                    is_numeric = false;
                                    break;
                                }
                            }
                            if (strncmp(cmd, "angle ", 6) == 0) {
                                int ang = atoi(cmd + 6);
                                claw_set_angle(ang);
                            } else if (is_numeric && cmd_len > 0) {
                                int ang = atoi(cmd);
                                claw_set_angle(ang);
                            } else if (strcmp(cmd, "half") == 0) {
                                claw_execute_command("half_open");
                            } else {
                                claw_execute_command(cmd); 
                            }
                        } else if (!is_cam_mode) {
                            int ang = 0;
                            if (strcmp(cmd, "sit") == 0 || strcmp(cmd, "stand") == 0 || strcmp(cmd, "forward") == 0 ||
                                strcmp(cmd, "backward") == 0 || strcmp(cmd, "step_forward") == 0 || strcmp(cmd, "step_backward") == 0 ||
                                strcmp(cmd, "stop") == 0 || strcmp(cmd, "crawl") == 0 || strcmp(cmd, "left_wave") == 0 || 
                                strcmp(cmd, "right_wave") == 0 || strcmp(cmd, "back_left_wave") == 0 || strcmp(cmd, "back_right_wave") == 0 ||
                                strcmp(cmd, "stretch_down") == 0 || strcmp(cmd, "stretch_back") == 0 || strcmp(cmd, "leap_forward") == 0) {
                                
                                ESP_LOGI("REPL", "Running Robot Action: %s", cmd);
                                servo_set_action(cmd);
                            }
                            else if (sscanf(cmd, "ll %d", &ang) == 1) { servo_set_target("low_left", ang); if(repl_sync) servo_set_target("low_right", ang); ESP_LOGI("REPL", "Low Left -> %d", ang); }
                            else if (sscanf(cmd, "lr %d", &ang) == 1) { servo_set_target("low_right", ang); if(repl_sync) servo_set_target("low_left", ang); ESP_LOGI("REPL", "Low Right -> %d", ang); }
                            else if (sscanf(cmd, "hl %d", &ang) == 1) { servo_set_target("high_left", ang); if(repl_sync) servo_set_target("high_right", ang); ESP_LOGI("REPL", "High Left -> %d", ang); }
                            else if (sscanf(cmd, "hr %d", &ang) == 1) { servo_set_target("high_right", ang); if(repl_sync) servo_set_target("high_left", ang); ESP_LOGI("REPL", "High Right -> %d", ang); }
                            else if (sscanf(cmd, "all %d", &ang) == 1) { servo_set_target("all", ang); ESP_LOGI("REPL", "All Legs -> %d", ang); }
                            else if (strcmp(cmd, "sync on") == 0) { repl_sync = true; ESP_LOGI("REPL", "Leg Sync ENABLED"); }
                            else if (strcmp(cmd, "sync off") == 0) { repl_sync = false; ESP_LOGI("REPL", "Leg Sync DISABLED"); }
                            else if (strncmp(cmd, "play ", 5) == 0) {
                                char sound_name[32] = {0};
                                sscanf(cmd + 5, "%31s", sound_name);
                                audio_play(sound_name);
                                ESP_LOGI("REPL", "Playing sound: %s", sound_name);
                            }
                            else if (strcmp(cmd, "sensor on") == 0) {
                                sensor_set_enabled(true);
                                ESP_LOGI("REPL", "Ultrasonic Sensor ENABLED");
                            }
                            else if (strcmp(cmd, "sensor off") == 0) {
                                sensor_set_enabled(false);
                                ESP_LOGI("REPL", "Ultrasonic Sensor DISABLED");
                            }
                            else {
                                ESP_LOGW("REPL", "Unknown Robot Command: %s", cmd);
                            }
                        }
                        cmd_idx = 0;
                    }
                } else {
                    if (cmd_idx < sizeof(cmd) - 1) {
                        cmd[cmd_idx++] = (char)buf[i];
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

extern "C" void app_main(void) {
    // 1. Initialize Non-Volatile Storage (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // --- Suppress Noisy Internal Framework Warnings ---
    esp_log_level_set("wifi", ESP_LOG_ERROR);    
    esp_log_level_set("cam_hal", ESP_LOG_NONE);  
    // --------------------------------------------------

    // 2. Setup Base Network Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Detect Saved Device & Boot Mode
    nvs_handle_t my_handle;
    char boot_mode[16] = "wifi";
    char dev_mode[16] = "robot";
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t len = sizeof(boot_mode);
        nvs_get_str(my_handle, "boot_mode", boot_mode, &len);
        len = sizeof(dev_mode);
        nvs_get_str(my_handle, "dev_mode", dev_mode, &len);
        nvs_close(my_handle);
    }
    
    is_claw_mode = (strcmp(dev_mode, "claw") == 0);
    is_cam_mode = (strcmp(dev_mode, "cam") == 0);

    // ALWAYS initialize BLE Manager on boot regardless of profile
    ble_manager_init();

    // 4. Boot Modular Components
    if (is_claw_mode) {
        ESP_LOGI(TAG, "Booting Device Profile: CLAW (with Camera support)");
        claw_controller_init();
        wifi_manager_init();
        cam_controller_init(); 
        web_server_init();
    } else if (is_cam_mode) {
        ESP_LOGI(TAG, "Booting Device Profile: CAM");
        wifi_manager_init(); 
        cam_controller_init();
        cam_espnow_init();
        web_server_init();
    } else {
        ESP_LOGI(TAG, "Booting Device Profile: ROBOT");
        cam_controller_init(); 
        audio_player_init(); 
        sensor_monitor_init();
        servo_controller_init();

        if (strcmp(boot_mode, "bt") == 0) {
            ESP_LOGI(TAG, "Booting in BLUETOOTH Mode. (Wi-Fi Disabled)");
        } else {
            ESP_LOGI(TAG, "Booting in WI-FI Mode.");
            wifi_manager_init();
            web_server_init();
        }
    }

    // 5. Start standard input keyboard listener for REPL commands
    xTaskCreate(console_read_task, "console_read_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Boot Sequence Complete!");
}