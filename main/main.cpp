// 11-3ESPSEEEDRobotNewN8R8/main/main.cpp
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/usb_serial_jtag.h"

// Bulletproof VFS inclusion across ESP-IDF versions
#if __has_include("esp_vfs_dev.h")
#include "esp_vfs_dev.h"
#endif
#if __has_include("esp_vfs_usb_serial_jtag.h")
#include "esp_vfs_usb_serial_jtag.h"
#endif
#if __has_include("driver/usb_serial_jtag_vfs.h")
#include "driver/usb_serial_jtag_vfs.h"
#endif

#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

// Modular Component Headers
#include "logger.h"
#include "wifi_manager.h"
#include "sensor_monitor.h"
#include "servo_controller.h"
#include "claw_controller.h"
#include "cam_controller.h"
#include "web_server.h"
#include "ble_manager.h"
#include "audio_player.h"

static const char *TAG = "MAIN";

bool is_claw_mode = false;
bool is_cam_mode = false;

// NON-BLOCKING console task: Works seamlessly on battery without USB host connection
static void console_read_task(void *pvParameter) {
    usb_serial_jtag_driver_config_t usbjtag_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usbjtag_cfg);

#if __has_include("driver/usb_serial_jtag_vfs.h")
    usb_serial_jtag_vfs_use_nonblocking();
#else
    esp_vfs_usb_serial_jtag_use_nonblocking();
#endif

    char cmd[128];
    int cmd_idx = 0;
    static bool repl_sync = false;
 
    while (1) {
        if (!usb_serial_jtag_is_connected()) {
            cmd_idx = 0;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint8_t c = 0;
        int len = usb_serial_jtag_read_bytes(&c, 1, 0);
 
        if (len > 0) {
            if (c == 0x03) {
                if (is_claw_mode) claw_execute_command("stop");
                else if (!is_cam_mode) servo_set_action("stop");
            } else if (c == '\r' || c == '\n') {
                if (cmd_idx > 0) {
                    cmd[cmd_idx] = '\0';
 
                    if (strcmp(cmd, "log") == 0 || strcmp(cmd, "logs") == 0 || strcmp(cmd, "cat log") == 0) {
                        logger_print_all();
                    } else if (strcmp(cmd, "log tail") == 0) {
                        logger_print_tail(50);
                    } else if (strcmp(cmd, "log clear") == 0 || strcmp(cmd, "clear log") == 0) {
                        logger_clear();
                    } else if (strcmp(cmd, "log size") == 0) {
                        printf("[LOGGER] Current debug log file size: %d bytes\n", (int)logger_get_size());
                    } else if (strcmp(cmd, "reboot") == 0) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                        esp_restart();
                    } else if (strcmp(cmd, "reset") == 0) {
                        nvs_handle_t h;
                        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_erase_all(h);
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(cmd, "mode claw") == 0) {
                        nvs_handle_t h;
                        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_str(h, "dev_mode", "claw");
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(cmd, "mode robot") == 0) {
                        nvs_handle_t h;
                        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_str(h, "dev_mode", "robot");
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(cmd, "mode cam") == 0) {
                        nvs_handle_t h;
                        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_str(h, "dev_mode", "cam");
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(cmd, "beep") == 0 || strcmp(cmd, "test") == 0) {
                        audio_play_test_beep();
                    } else if (strcmp(cmd, "bark") == 0 || strcmp(cmd, "play") == 0) {
                        audio_play("dog_bark");
                    } else if (strcmp(cmd, "stop sound") == 0) {
                        audio_stop();
                    } else if (strncmp(cmd, "vol ", 4) == 0 || strncmp(cmd, "volume ", 7) == 0) {
                        int req_vol = (strncmp(cmd, "vol ", 4) == 0) ? atoi(cmd + 4) : atoi(cmd + 7);
                        audio_set_volume(req_vol);
                    } else if (strncmp(cmd, "pins ", 5) == 0) {
                        int b = 0, l = 0, d = 0;
                        if (sscanf(cmd + 5, "%d %d %d", &b, &l, &d) == 3) {
                            audio_set_pins(b, l, d);
                        }
                    } else if (strcmp(cmd, "status") == 0) {
                        int bclk = 10, lrc = 11, din = 12;
                        audio_get_pins(&bclk, &lrc, &din);
                        int ll, hr, hl, lr;
                        servo_get_angles(&ll, &hr, &hl, &lr);
                        ESP_LOGI("REPL", "Servos(LL:%d, HR:%d, HL:%d, LR:%d) | Vol:%d%% | Audio Pins: BCLK=%d, LRC=%d, DIN=%d",
                                 ll, hr, hl, lr, audio_get_volume(), bclk, lrc, din);
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
                            claw_set_angle(atoi(cmd + 6));
                        } else if (is_numeric && cmd_len > 0) {
                            claw_set_angle(atoi(cmd));
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
                            servo_set_action(cmd);
                        }
                        else if (sscanf(cmd, "ll %d", &ang) == 1) { servo_set_target("low_left", ang); if(repl_sync) servo_set_target("low_right", ang); }
                        else if (sscanf(cmd, "lr %d", &ang) == 1) { servo_set_target("low_right", ang); if(repl_sync) servo_set_target("low_left", ang); }
                        else if (sscanf(cmd, "hl %d", &ang) == 1) { servo_set_target("high_left", ang); if(repl_sync) servo_set_target("high_right", ang); }
                        else if (sscanf(cmd, "hr %d", &ang) == 1) { servo_set_target("high_right", ang); if(repl_sync) servo_set_target("high_left", ang); }
                        else if (sscanf(cmd, "all %d", &ang) == 1) { servo_set_target("all", ang); }
                        else if (strcmp(cmd, "sync on") == 0) { repl_sync = true; }
                        else if (strcmp(cmd, "sync off") == 0) { repl_sync = false; }
                        else if (strcmp(cmd, "sensor on") == 0) {
                            sensor_set_enabled(true);
                            ESP_LOGI("REPL", "Sensor ENABLED");
                        }
                        else if (strcmp(cmd, "sensor off") == 0) {
                            sensor_set_enabled(false);
                            ESP_LOGI("REPL", "Sensor DISABLED");
                        }
                    }
                    cmd_idx = 0;
                }
            } else {
                if (cmd_idx < (int)sizeof(cmd) - 1) {
                    cmd[cmd_idx++] = (char)c;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern "C" void app_main(void) {
    // 1. Hardware clamp servo pins to strict 0V output
    gpio_config_t servo_io_cfg = {};
    servo_io_cfg.intr_type    = GPIO_INTR_DISABLE;
    servo_io_cfg.mode         = GPIO_MODE_OUTPUT;
    servo_io_cfg.pin_bit_mask = (1ULL << GPIO_NUM_1) | (1ULL << GPIO_NUM_2) |
                                (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_4);
    servo_io_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    servo_io_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    gpio_config(&servo_io_cfg);

    gpio_set_level(GPIO_NUM_1, 0);
    gpio_set_level(GPIO_NUM_2, 0);
    gpio_set_level(GPIO_NUM_3, 0);
    gpio_set_level(GPIO_NUM_4, 0);

    rtc_gpio_pullup_dis(GPIO_NUM_1);
    rtc_gpio_pulldown_en(GPIO_NUM_1);
    rtc_gpio_pullup_dis(GPIO_NUM_2);
    rtc_gpio_pulldown_en(GPIO_NUM_2);
    rtc_gpio_pullup_dis(GPIO_NUM_3);
    rtc_gpio_pulldown_en(GPIO_NUM_3);
    rtc_gpio_pullup_dis(GPIO_NUM_4);
    rtc_gpio_pulldown_en(GPIO_NUM_4);

    // 2. Initialize Non-Volatile Storage (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 3. Initialize Flash File Logger
    logger_init();

    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("cam_hal", ESP_LOG_NONE);

    // 4. Setup Base Network Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 5. Detect Saved Device & Boot Mode
    nvs_handle_t my_handle;
    char dev_mode[16] = "robot";
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t len = sizeof(dev_mode);
        nvs_get_str(my_handle, "dev_mode", dev_mode, &len);
        nvs_close(my_handle);
    }
 
    is_claw_mode = (strcmp(dev_mode, "claw") == 0);
    is_cam_mode = (strcmp(dev_mode, "cam") == 0);

    // 6. Initialize Wi-Fi and Web Server
    ESP_LOGI(TAG, "Starting Wi-Fi and Web Server...");
    wifi_manager_init();
    web_server_start(); // Refactored to unified start API

    // 7. Initialize Bluetooth Low Energy Engine (Nordic UART Service)
    ESP_LOGI(TAG, "Starting Bluetooth LE Subsystem for iPhone app connection...");
    ble_manager_init();

    // 8. Initialize Peripherals based on mode
    if (is_claw_mode) {
        ESP_LOGI(TAG, "Booting Device Profile: CLAW");
        claw_controller_init();
        cam_controller_init();
    } else if (is_cam_mode) {
        ESP_LOGI(TAG, "Booting Device Profile: CAM");
        cam_controller_init();
        cam_espnow_init();
    } else {
        ESP_LOGI(TAG, "Booting Device Profile: ROBOT");
        cam_controller_init();
        audio_player_init();
        sensor_monitor_init();
        servo_controller_init();
    }

    // 9. Start USB Serial/JTAG console task
    xTaskCreate(console_read_task, "console_read_task", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "Boot Sequence Complete! Bluetooth, Wi-Fi, Servos and Audio are Active.");
}