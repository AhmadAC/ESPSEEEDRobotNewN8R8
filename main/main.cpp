
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

static void print_repl_help(void) {
    printf("\n=======================================================================\n");
    printf("                    ESP32-S3 ROBOT REPL COMMAND HELP                  \n");
    printf("=======================================================================\n");
    printf("[MOTION & GAITS]\n");
    printf("  stand                   : Align joints to 90 deg standing pose\n");
    printf("  sit                     : Fold all legs into sitting pose\n");
    printf("  walk / forward          : Walk forward with fast diagonal trot gait\n");
    printf("  backward / walk back    : Walk backward with diagonal trot gait\n");
    printf("  step_forward            : Take a single forward step\n");
    printf("  step_backward           : Take a single backward step\n");
    printf("  stop                    : Emergency stop & relax all servos (0A draw)\n");
    printf("  crawl                   : Low-profile crawling gait\n");
    printf("  leap_forward            : Leap forward\n");
    printf("  left_wave / right_wave  : Wave front left or front right leg\n");
    printf("  back_left_wave          : Wave back left leg\n");
    printf("  back_right_wave         : Wave back right leg\n");
    printf("  stretch_down            : Bow stretch down\n");
    printf("  stretch_back            : Push stretch back\n");
    printf("\n[INDIVIDUAL JOINT CONTROL]\n");
    printf("  hl <0-180>              : Set Front Left leg angle (e.g. 'hl 90')\n");
    printf("  hr <0-180>              : Set Front Right leg angle\n");
    printf("  ll <0-180>              : Set Back Left leg angle\n");
    printf("  lr <0-180>              : Set Back Right leg angle\n");
    printf("  all <0-180>             : Set all 4 legs to target angle\n");
    printf("  sync on / sync off      : Synchronize paired legs in REPL\n");
    printf("\n[AUDIO & SPEAKER (HT517)]\n");
    printf("  bark / play             : Play embedded dog bark audio\n");
    printf("  beep / test             : Play high-low diagnostic chime\n");
    printf("  stop sound              : Mute and abort current sound playback\n");
    printf("  vol <0-100>             : Set speaker volume (e.g. 'vol 80')\n");
    printf("  pins <bclk> <lrc> <din> : Reassign I2S speaker pins (e.g. 'pins 10 11 12')\n");
    printf("\n[ULTRASONIC SENSOR]\n");
    printf("  sensor on / sensor off  : Arm or disarm automatic obstacle safety\n");
    printf("  dist                    : Read live distance in centimeters\n");
    printf("\n[SYSTEM & FLASH LOGS]\n");
    printf("  status                  : Print robot angles, volume, pins, and state\n");
    printf("  log / logs / cat log    : Dump entire persistent SPIFFS debug log\n");
    printf("  log tail                : Display the last 50 lines of debug log\n");
    printf("  log clear               : Clear persistent flash debug log\n");
    printf("  log size                : Check debug log file size in bytes\n");
    printf("  mode robot/claw/cam     : Switch active device operating profile\n");
    printf("  reset                   : Factory reset NVS flash configuration\n");
    printf("  reboot                  : Reboot the ESP32-S3 microcontroller\n");
    printf("  help / info / ?         : Display this command manual\n");
    printf("=======================================================================\n\n");
}

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
                printf("[REPL] Emergency Stop Triggered\n");
            } else if (c == '\r' || c == '\n') {
                if (cmd_idx > 0) {
                    cmd[cmd_idx] = '\0';

                    // Trim leading spaces
                    char *p = cmd;
                    while (*p == ' ' || *p == '\t') p++;

                    if (strcmp(p, "help") == 0 || strcmp(p, "?") == 0 || strcmp(p, "info") == 0 || strcmp(p, "commands") == 0) {
                        print_repl_help();
                    } else if (strcmp(p, "log") == 0 || strcmp(p, "logs") == 0 || strcmp(p, "cat log") == 0) {
                        logger_print_all();
                    } else if (strcmp(p, "log tail") == 0) {
                        logger_print_tail(50);
                    } else if (strcmp(p, "log clear") == 0 || strcmp(p, "clear log") == 0) {
                        logger_clear();
                    } else if (strcmp(p, "log size") == 0) {
                        printf("[LOGGER] Current debug log file size: %d bytes\n", (int)logger_get_size());
                    } else if (strcmp(p, "reboot") == 0) {
                        printf("[REPL] Rebooting...\n");
                        vTaskDelay(pdMS_TO_TICKS(100));
                        esp_restart();
                    } else if (strcmp(p, "reset") == 0) {
                        printf("[REPL] Resetting NVS storage to defaults...\n");
                        nvs_handle_t h;
                        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_erase_all(h);
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(p, "mode claw") == 0) {
                        nvs_handle_t h;
                        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_str(h, "dev_mode", "claw");
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        printf("[REPL] Profile set to CLAW. Rebooting...\n");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(p, "mode robot") == 0) {
                        nvs_handle_t h;
                        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_str(h, "dev_mode", "robot");
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        printf("[REPL] Profile set to ROBOT. Rebooting...\n");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(p, "mode cam") == 0) {
                        nvs_handle_t h;
                        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_str(h, "dev_mode", "cam");
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        printf("[REPL] Profile set to CAM. Rebooting...\n");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(p, "beep") == 0 || strcmp(p, "test") == 0) {
                        printf("[REPL] Playing chime test...\n");
                        audio_play_test_beep();
                    } else if (strcmp(p, "bark") == 0 || strcmp(p, "play") == 0) {
                        printf("[REPL] Playing dog bark audio...\n");
                        audio_play("dog_bark");
                    } else if (strcmp(p, "stop sound") == 0) {
                        printf("[REPL] Sound stopped.\n");
                        audio_stop();
                    } else if (strncmp(p, "vol ", 4) == 0 || strncmp(p, "volume ", 7) == 0) {
                        int req_vol = (strncmp(p, "vol ", 4) == 0) ? atoi(p + 4) : atoi(p + 7);
                        audio_set_volume(req_vol);
                        printf("[REPL] Volume set to %d%%\n", req_vol);
                    } else if (strncmp(p, "pins ", 5) == 0) {
                        int b = 0, l = 0, d = 0;
                        if (sscanf(p + 5, "%d %d %d", &b, &l, &d) == 3) {
                            audio_set_pins(b, l, d);
                            printf("[REPL] Audio pins updated: BCLK=%d, LRC=%d, DIN=%d\n", b, l, d);
                        }
                    } else if (strcmp(p, "dist") == 0) {
                        printf("[SENSOR] Distance: %.1f cm (Safety: %s)\n",
                               (double)sensor_get_distance(), sensor_is_enabled() ? "ARMED" : "DISABLED");
                    } else if (strcmp(p, "status") == 0) {
                        int bclk = 10, lrc = 11, din = 12;
                        audio_get_pins(&bclk, &lrc, &din);
                        int ll = 90, hr = 90, hl = 90, lr = 90;
                        servo_get_angles(&ll, &hr, &hl, &lr);
                        printf("[STATUS] Servos(LL:%d, HR:%d, HL:%d, LR:%d) | Vol:%d%% | Audio(BCLK:%d, LRC:%d, DIN:%d) | Sensor: %.1f cm (%s)\n",
                               ll, hr, hl, lr, audio_get_volume(), bclk, lrc, din,
                               (double)sensor_get_distance(), sensor_is_enabled() ? "ARMED" : "DISABLED");
                    } else if (is_claw_mode) {
                        bool is_numeric = true;
                        int cmd_len = strlen(p);
                        for (int j = 0; j < cmd_len; j++) {
                            if (p[j] < '0' || p[j] > '9') {
                                is_numeric = false;
                                break;
                            }
                        }
                        if (strncmp(p, "angle ", 6) == 0) {
                            int a = atoi(p + 6);
                            claw_set_angle(a);
                            printf("[REPL] Claw angle set to %d\n", a);
                        } else if (is_numeric && cmd_len > 0) {
                            int a = atoi(p);
                            claw_set_angle(a);
                            printf("[REPL] Claw angle set to %d\n", a);
                        } else if (strcmp(p, "half") == 0) {
                            claw_execute_command("half_open");
                            printf("[REPL] Claw half-opened\n");
                        } else {
                            claw_execute_command(p);
                            printf("[REPL] Claw command: %s\n", p);
                        }
                    } else if (!is_cam_mode) {
                        int ang = 0;
                        if (strcmp(p, "walk") == 0 || strcmp(p, "forward") == 0 || strcmp(p, "walk forward") == 0 || strcmp(p, "walk_forward") == 0) {
                            printf("[REPL] Action: walk forward (fast trot)\n");
                            servo_set_action("forward");
                        } else if (strcmp(p, "backward") == 0 || strcmp(p, "walk back") == 0 || strcmp(p, "walk backward") == 0 || strcmp(p, "walk_backward") == 0) {
                            printf("[REPL] Action: walk backward (fast trot)\n");
                            servo_set_action("backward");
                        } else if (strcmp(p, "stand") == 0) {
                            printf("[REPL] Action: stand\n");
                            servo_set_action("stand");
                        } else if (strcmp(p, "sit") == 0) {
                            printf("[REPL] Action: sit\n");
                            servo_set_action("sit");
                        } else if (strcmp(p, "step_forward") == 0 || strcmp(p, "step") == 0) {
                            printf("[REPL] Action: step forward\n");
                            servo_set_action("step_forward");
                        } else if (strcmp(p, "step_backward") == 0) {
                            printf("[REPL] Action: step backward\n");
                            servo_set_action("step_backward");
                        } else if (strcmp(p, "stop") == 0) {
                            printf("[REPL] Action: stop (servos relaxed)\n");
                            servo_set_action("stop");
                        } else if (strcmp(p, "crawl") == 0) {
                            printf("[REPL] Action: crawl\n");
                            servo_set_action("crawl");
                        } else if (strcmp(p, "left_wave") == 0) {
                            printf("[REPL] Action: left wave\n");
                            servo_set_action("left_wave");
                        } else if (strcmp(p, "right_wave") == 0) {
                            printf("[REPL] Action: right wave\n");
                            servo_set_action("right_wave");
                        } else if (strcmp(p, "back_left_wave") == 0) {
                            printf("[REPL] Action: back left wave\n");
                            servo_set_action("back_left_wave");
                        } else if (strcmp(p, "back_right_wave") == 0) {
                            printf("[REPL] Action: back right wave\n");
                            servo_set_action("back_right_wave");
                        } else if (strcmp(p, "stretch_down") == 0) {
                            printf("[REPL] Action: stretch down\n");
                            servo_set_action("stretch_down");
                        } else if (strcmp(p, "stretch_back") == 0) {
                            printf("[REPL] Action: stretch back\n");
                            servo_set_action("stretch_back");
                        } else if (strcmp(p, "leap_forward") == 0 || strcmp(p, "leap") == 0) {
                            printf("[REPL] Action: leap forward\n");
                            servo_set_action("leap_forward");
                        } else if (sscanf(p, "ll %d", &ang) == 1) { 
                            servo_set_target("low_left", ang); 
                            if (repl_sync) servo_set_target("low_right", ang); 
                            printf("[REPL] Joint LL: %d deg\n", ang);
                        } else if (sscanf(p, "lr %d", &ang) == 1) { 
                            servo_set_target("low_right", ang); 
                            if (repl_sync) servo_set_target("low_left", ang); 
                            printf("[REPL] Joint LR: %d deg\n", ang);
                        } else if (sscanf(p, "hl %d", &ang) == 1) { 
                            servo_set_target("high_left", ang); 
                            if (repl_sync) servo_set_target("high_right", ang); 
                            printf("[REPL] Joint HL: %d deg\n", ang);
                        } else if (sscanf(p, "hr %d", &ang) == 1) { 
                            servo_set_target("high_right", ang); 
                            if (repl_sync) servo_set_target("high_left", ang); 
                            printf("[REPL] Joint HR: %d deg\n", ang);
                        } else if (sscanf(p, "all %d", &ang) == 1) { 
                            servo_set_target("all", ang); 
                            printf("[REPL] All Joints: %d deg\n", ang);
                        } else if (strcmp(p, "sync on") == 0) { 
                            repl_sync = true; 
                            printf("[REPL] Leg Joint Sync ON\n");
                        } else if (strcmp(p, "sync off") == 0) { 
                            repl_sync = false; 
                            printf("[REPL] Leg Joint Sync OFF\n");
                        } else if (strcmp(p, "sensor on") == 0) {
                            sensor_set_enabled(true);
                            printf("[REPL] Sensor Safety ARMED\n");
                        } else if (strcmp(p, "sensor off") == 0) {
                            sensor_set_enabled(false);
                            printf("[REPL] Sensor Safety DISARMED\n");
                        } else {
                            printf("[REPL] Unknown command: '%s'. Type 'help' for available commands.\n", p);
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
    // 1. Initialize NVS FIRST to read configured pins
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Read configured servo pins from NVS
    int32_t pin_ll = 1, pin_hr = 2, pin_hl = 3, pin_lr = 4;
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "pin_ll", &pin_ll);
        nvs_get_i32(h, "pin_hr", &pin_hr);
        nvs_get_i32(h, "pin_hl", &pin_hl);
        nvs_get_i32(h, "pin_lr", &pin_lr);
        nvs_close(h);
    }
    
    if (pin_ll <= 0 || pin_ll > 48) pin_ll = 1;
    if (pin_hr <= 0 || pin_hr > 48) pin_hr = 2;
    if (pin_hl <= 0 || pin_hl > 48) pin_hl = 3;
    if (pin_lr <= 0 || pin_lr > 48) pin_lr = 4;

    // 2. Zero-Glitch Boot-Time Clamping on ACTUAL pins
    gpio_config_t servo_io_cfg = {};
    servo_io_cfg.intr_type    = GPIO_INTR_DISABLE;
    servo_io_cfg.mode         = GPIO_MODE_OUTPUT;
    servo_io_cfg.pin_bit_mask = (1ULL << pin_ll) | (1ULL << pin_hr) |
                                (1ULL << pin_hl) | (1ULL << pin_lr);
    servo_io_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    servo_io_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    gpio_config(&servo_io_cfg);

    gpio_set_level((gpio_num_t)pin_ll, 0);
    gpio_set_level((gpio_num_t)pin_hr, 0);
    gpio_set_level((gpio_num_t)pin_hl, 0);
    gpio_set_level((gpio_num_t)pin_lr, 0);

    rtc_gpio_pullup_dis((gpio_num_t)pin_ll);
    rtc_gpio_pulldown_en((gpio_num_t)pin_ll);
    rtc_gpio_pullup_dis((gpio_num_t)pin_hr);
    rtc_gpio_pulldown_en((gpio_num_t)pin_hr);
    rtc_gpio_pullup_dis((gpio_num_t)pin_hl);
    rtc_gpio_pulldown_en((gpio_num_t)pin_hl);
    rtc_gpio_pullup_dis((gpio_num_t)pin_lr);
    rtc_gpio_pulldown_en((gpio_num_t)pin_lr);

    // 3. Initialize Logger
    logger_init();

    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("cam_hal", ESP_LOG_NONE);

    // 4. Network Setup
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 5. Detect Mode
    char dev_mode[16] = "robot";
    if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(dev_mode);
        nvs_get_str(h, "dev_mode", dev_mode, &len);
        nvs_close(h);
    }
 
    is_claw_mode = (strcmp(dev_mode, "claw") == 0);
    is_cam_mode = (strcmp(dev_mode, "cam") == 0);

    // 6. Start Services
    ESP_LOGI(TAG, "Starting Wi-Fi and Web Server...");
    wifi_manager_init();
    web_server_start();

    ESP_LOGI(TAG, "Starting Bluetooth LE Subsystem...");
    ble_manager_init();

    // 7. Init Peripherals
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

    // 8. Console Task
    xTaskCreate(console_read_task, "console_read_task", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "Boot Sequence Complete! Type 'help' in terminal for commands.");
}