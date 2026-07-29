#include "servo_controller.h"
#include "sensor_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SERVO";

// Pinout mapping for Seeed XIAO ESP32S3 / ESP32-S3 Zero
#define SERVO_LOW_LEFT_PIN   GPIO_NUM_1 // D0 (Back Left)
#define SERVO_HIGH_RIGHT_PIN GPIO_NUM_2 // D1 (Front Right)
#define SERVO_HIGH_LEFT_PIN  GPIO_NUM_3 // D2 (Front Left)
#define SERVO_LOW_RIGHT_PIN  GPIO_NUM_4 // D3 (Back Right)

#define SERVO_LOW_LEFT_CH    LEDC_CHANNEL_0
#define SERVO_HIGH_RIGHT_CH  LEDC_CHANNEL_2
#define SERVO_HIGH_LEFT_CH   LEDC_CHANNEL_3
#define SERVO_LOW_RIGHT_CH   LEDC_CHANNEL_4

// State Variables (Logical angles: 0 = Left, 180 = Right)
static int32_t target_low_left   = 90;
static int32_t target_high_right = 90;
static int32_t target_high_left  = 90;
static int32_t target_low_right  = 90;

static int active_animation = 0; // 0=None, 1=Walk Forward, 2=Walk Back, 3=Step Forward, 4=Step Back, 5=Left Wave, 6=Right Wave, 7=Crawl, 8=SitToStand, 9=BackLeftWave, 10=BackRightWave, 11=LeapForward
static int anim_state = 0;
static int anim_pos = 90;
static int wait_counter = 0;

static int32_t offset_low_left   = 0;
static int32_t offset_high_right = 0;
static int32_t offset_high_left  = 0;
static int32_t offset_low_right  = 11;

struct Pose { int32_t ll, hr, hl, lr; };
static Pose poses[5] = {
    {0, 0, 0, 0},        // sit
    {90, 90, 90, 90},    // stand
    {90, 180, 180, 90},  // stretch_down
    {0, 90, 90, 0},      // stretch_back
    {90, 90, 90, 90}     // stop
};

static const char* pose_names[5] = {"sit", "stand", "stretch_down", "stretch_back", "stop"};
static const char* pose_keys[5]  = {"sit", "std", "sdn", "sbk", "stp"};

static void write_servo_calibrated(ledc_channel_t channel, int32_t target_angle, int32_t offset) {
    int32_t final_angle = target_angle + offset;
    
    if (channel == SERVO_LOW_RIGHT_CH) {
        if (final_angle < 4) final_angle = 4;
    } else {
        if (final_angle < 0) final_angle = 0;
    }
    
    if (final_angle > 180) final_angle = 180;
    
    uint32_t duty = 204 + ((1024 - 204) * final_angle) / 180;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

static void apply_all_servos() {
    write_servo_calibrated(SERVO_LOW_LEFT_CH,   180 - target_low_left,  -offset_low_left);
    write_servo_calibrated(SERVO_HIGH_RIGHT_CH, target_high_right,       offset_high_right);
    write_servo_calibrated(SERVO_HIGH_LEFT_CH,  180 - target_high_left, -offset_high_left);
    write_servo_calibrated(SERVO_LOW_RIGHT_CH,  target_low_right,        offset_low_right);
}

static bool current_is_sit() {
    return (target_low_left == poses[0].ll && 
            target_high_right == poses[0].hr && 
            target_high_left == poses[0].hl && 
            target_low_right == poses[0].lr);
}

static void servo_animation_task(void *pv) {
    while(1) {
        if (active_animation == 1) { 
            if (anim_state == 0) {
                anim_pos += 4;
                if (anim_pos >= 130) anim_state = 1;
            } else if (anim_state == 1) {
                anim_pos -= 4;
                if (anim_pos <= 50) anim_state = 0;
            }
            
            target_high_left = anim_pos;
            target_low_right = anim_pos;
            target_high_right = 180 - anim_pos;
            target_low_left = 180 - anim_pos;
            
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(20));
            
        } else if (active_animation == 2) {
            if (anim_state == 0) {
                anim_pos -= 4;
                if (anim_pos <= 50) anim_state = 1;
            } else if (anim_state == 1) {
                anim_pos += 4;
                if (anim_pos >= 130) anim_state = 0;
            }
            
            target_high_left = anim_pos;
            target_low_right = anim_pos;
            target_high_right = 180 - anim_pos;
            target_low_left = 180 - anim_pos;
            
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(20));
            
        } else if (active_animation == 3) {
            if (anim_state == 0) {
                anim_pos += 4;
                if (anim_pos >= 130) anim_state = 1;
            } else if (anim_state == 1) {
                anim_pos -= 4;
                if (anim_pos <= 50) anim_state = 2;
            } else if (anim_state == 2) {
                anim_pos += 4;
                if (anim_pos >= 90) {
                    anim_pos = 90;
                    active_animation = 0;
                }
            }
            
            target_high_left = anim_pos;
            target_low_right = anim_pos;
            target_high_right = 180 - anim_pos;
            target_low_left = 180 - anim_pos;
            
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(20));
            
        } else if (active_animation == 4) {
            if (anim_state == 0) {
                anim_pos -= 4;
                if (anim_pos <= 50) anim_state = 1;
            } else if (anim_state == 1) {
                anim_pos += 4;
                if (anim_pos >= 130) anim_state = 2;
            } else if (anim_state == 2) {
                anim_pos -= 4;
                if (anim_pos <= 90) {
                    anim_pos = 90;
                    active_animation = 0;
                }
            }
            
            target_high_left = anim_pos;
            target_low_right = anim_pos;
            target_high_right = 180 - anim_pos;
            target_low_left = 180 - anim_pos;
            
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(20));
            
        } else if (active_animation == 5) {
            int step = 20;
            if (anim_state == 0) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 1; }
            } else if (anim_state == 1) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 2; }
            } else if (anim_state == 2) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 3; }
            } else if (anim_state == 3) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 4; }
            } else if (anim_state == 4) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 5; }
            } else if (anim_state == 5) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 6; }
            } else if (anim_state == 6) {
                anim_pos += step;
                if (anim_pos >= 90) { 
                    anim_pos = 90; 
                    active_animation = 0;
                }
            }
            target_high_left = anim_pos;
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(15));
            
        } else if (active_animation == 6) {
            int step = 20; 
            if (anim_state == 0) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 1; }
            } else if (anim_state == 1) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 2; }
            } else if (anim_state == 2) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 3; }
            } else if (anim_state == 3) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 4; }
            } else if (anim_state == 4) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 5; }
            } else if (anim_state == 5) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 6; }
            } else if (anim_state == 6) {
                anim_pos += step;
                if (anim_pos >= 90) { 
                    anim_pos = 90; 
                    active_animation = 0;
                }
            }
            target_high_right = anim_pos;
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(15));
            
        } else if (active_animation == 7) {
            if (anim_state == 0) {
                anim_pos += 5;
                if (anim_pos >= 135) anim_state = 1;
            } else if (anim_state == 1) {
                anim_pos -= 5;
                if (anim_pos <= 45) anim_state = 0;
            }
            
            target_low_left = 0;
            target_low_right = 0;
            
            target_high_left = anim_pos;
            target_high_right = 180 - anim_pos;
            
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(20));
            
        } else if (active_animation == 8) {
            if (anim_state == 0) {
                target_high_right = 90;
                apply_all_servos();
                wait_counter = 0;
                anim_state = 1;
            } else if (anim_state == 1) {
                wait_counter++;
                if (wait_counter >= 10) {
                    target_low_right = 90;
                    apply_all_servos();
                    wait_counter = 0;
                    anim_state = 2;
                }
            } else if (anim_state == 2) {
                wait_counter++;
                if (wait_counter >= 10) {
                    target_high_left = 90;
                    apply_all_servos();
                    wait_counter = 0;
                    anim_state = 3;
                }
            } else if (anim_state == 3) {
                wait_counter++;
                if (wait_counter >= 10) {
                    target_low_left = 90;
                    apply_all_servos();
                    wait_counter = 0;
                    anim_state = 4;
                }
            } else if (anim_state == 4) {
                wait_counter++;
                if (wait_counter >= 10) {
                    active_animation = 0;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            
        } else if (active_animation == 9) {
            int step = 20; 
            if (anim_state == 0) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 1; }
            } else if (anim_state == 1) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 2; }
            } else if (anim_state == 2) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 3; }
            } else if (anim_state == 3) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 4; }
            } else if (anim_state == 4) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 5; }
            } else if (anim_state == 5) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 6; }
            } else if (anim_state == 6) {
                anim_pos += step;
                if (anim_pos >= 90) { 
                    anim_pos = 90; 
                    active_animation = 0; 
                }
            }
            target_low_left = anim_pos;
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(15));

        } else if (active_animation == 10) {
            int step = 20; 
            if (anim_state == 0) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 1; }
            } else if (anim_state == 1) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 2; }
            } else if (anim_state == 2) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 3; }
            } else if (anim_state == 3) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 4; }
            } else if (anim_state == 4) {
                anim_pos += step;
                if (anim_pos >= 180) { anim_pos = 180; anim_state = 5; }
            } else if (anim_state == 5) {
                anim_pos -= step;
                if (anim_pos <= 0) { anim_pos = 0; anim_state = 6; }
            } else if (anim_state == 6) {
                anim_pos += step;
                if (anim_pos >= 90) { 
                    anim_pos = 90; 
                    active_animation = 0; 
                }
            }
            target_low_right = anim_pos;
            apply_all_servos();
            vTaskDelay(pdMS_TO_TICKS(15));

        } else if (active_animation == 11) {
            if (anim_state == 0) {
                target_high_left = 180;
                target_high_right = 180;
                apply_all_servos();
                wait_counter = 0;
                anim_state = 1;
            } else if (anim_state == 1) {
                wait_counter++;
                if (wait_counter >= 25) {
                    target_low_left = 180;
                    target_low_right = 180;
                    apply_all_servos();
                    wait_counter = 0;
                    anim_state = 2;
                }
            } else if (anim_state == 2) {
                wait_counter++;
                if (wait_counter >= 25) {
                    target_low_left = 0;
                    target_low_right = 0;
                    apply_all_servos();
                    wait_counter = 0;
                    anim_state = 3;
                }
            } else if (anim_state == 3) {
                wait_counter++;
                if (wait_counter >= 25) {
                    anim_state = 1;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void servo_controller_init() {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_i32(my_handle, "ll_off", &offset_low_left);
        nvs_get_i32(my_handle, "hr_off", &offset_high_right);
        nvs_get_i32(my_handle, "hl_off", &offset_high_left);
        nvs_get_i32(my_handle, "lr_off", &offset_low_right);

        for (int i = 0; i < 5; i++) {
            char key[16];
            snprintf(key, sizeof(key), "p_%s_ll", pose_keys[i]); nvs_get_i32(my_handle, key, &poses[i].ll);
            snprintf(key, sizeof(key), "p_%s_hr", pose_keys[i]); nvs_get_i32(my_handle, key, &poses[i].hr);
            snprintf(key, sizeof(key), "p_%s_hl", pose_keys[i]); nvs_get_i32(my_handle, key, &poses[i].hl);
            snprintf(key, sizeof(key), "p_%s_lr", pose_keys[i]); nvs_get_i32(my_handle, key, &poses[i].lr);
        }
        nvs_close(my_handle);
    }

    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode       = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution  = LEDC_TIMER_13_BIT;
    ledc_timer.timer_num        = LEDC_TIMER_0;
    ledc_timer.freq_hz          = 50;  
    ledc_timer.clk_cfg          = LEDC_AUTO_CLK;
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t chan_cfg = {};
    chan_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    chan_cfg.intr_type  = LEDC_INTR_DISABLE;
    chan_cfg.timer_sel  = LEDC_TIMER_0;
    chan_cfg.duty = 0; chan_cfg.hpoint = 0;

    chan_cfg.gpio_num = SERVO_LOW_LEFT_PIN;   chan_cfg.channel = SERVO_LOW_LEFT_CH;   ledc_channel_config(&chan_cfg);
    chan_cfg.gpio_num = SERVO_HIGH_RIGHT_PIN; chan_cfg.channel = SERVO_HIGH_RIGHT_CH; ledc_channel_config(&chan_cfg);
    chan_cfg.gpio_num = SERVO_HIGH_LEFT_PIN;  chan_cfg.channel = SERVO_HIGH_LEFT_CH;  ledc_channel_config(&chan_cfg);
    chan_cfg.gpio_num = SERVO_LOW_RIGHT_PIN;  chan_cfg.channel = SERVO_LOW_RIGHT_CH;  ledc_channel_config(&chan_cfg);

    active_animation = 0;
    target_low_left   = 90;
    target_high_right = 90;
    target_high_left  = 90;
    target_low_right  = 90;
    
    xTaskCreatePinnedToCore(servo_animation_task, "anim_task", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "Hardware PWM Initialized (Relaxed on boot)");
}

void servo_set_target(const char* id, int angle) {
    if (sensor_is_safety_locked()) return;
    active_animation = 0;
    
    if (strcmp(id, "low_left") == 0)   target_low_left = angle;
    if (strcmp(id, "high_right") == 0) target_high_right = angle;
    if (strcmp(id, "high_left") == 0)  target_high_left = angle;
    if (strcmp(id, "low_right") == 0)  target_low_right = angle;
    
    if (strcmp(id, "all") == 0) {
        target_low_left = angle; target_high_right = angle; target_high_left = angle; target_low_right = angle;
    }
    
    apply_all_servos();
}

void servo_set_target_silent(const char* id, int angle) {
    if (sensor_is_safety_locked()) return;
    active_animation = 0;
    
    if (strcmp(id, "low_left") == 0)   target_low_left = angle;
    if (strcmp(id, "high_right") == 0) target_high_right = angle;
    if (strcmp(id, "high_left") == 0)  target_high_left = angle;
    if (strcmp(id, "low_right") == 0)  target_low_right = angle;
}

void servo_apply_targets() {
    if (sensor_is_safety_locked()) return;
    apply_all_servos();
}

void servo_set_action(const char* action_name) {
    if (sensor_is_safety_locked()) return;
    servo_set_action_bypass(action_name);
}

void servo_set_action_bypass(const char* action_name) {
    if (strcmp(action_name, "forward") == 0) {
        if (active_animation != 1) {
            active_animation = 1;
            anim_state = 0;
            anim_pos = 90;
            wait_counter = 0;
        }
    } else if (strcmp(action_name, "backward") == 0) {
        if (active_animation != 2) {
            active_animation = 2;
            anim_state = 0;
            anim_pos = 90;
            wait_counter = 0;
        }
    } else if (strcmp(action_name, "step_forward") == 0) {
        active_animation = 3;
        anim_state = 0;
        anim_pos = 90;
        wait_counter = 0;
    } else if (strcmp(action_name, "step_backward") == 0) {
        active_animation = 4;
        anim_state = 0;
        anim_pos = 90;
        wait_counter = 0;
    } else if (strcmp(action_name, "left_wave") == 0) {
        active_animation = 5;
        anim_state = 0;
        anim_pos = 90;
        wait_counter = 0;
    } else if (strcmp(action_name, "right_wave") == 0) {
        active_animation = 6;
        anim_state = 0;
        anim_pos = 90;
        wait_counter = 0;
    } else if (strcmp(action_name, "crawl") == 0) {
        active_animation = 7;
        anim_state = 0;
        anim_pos = 90;
        wait_counter = 0;
    } else if (strcmp(action_name, "stand") == 0) {
        if (current_is_sit()) {
            active_animation = 8;
            anim_state = 0;
            anim_pos = 90;
            wait_counter = 0;
        } else {
            active_animation = 0;
            target_low_left   = poses[1].ll;
            target_high_right = poses[1].hr;
            target_high_left  = poses[1].hl;
            target_low_right  = poses[1].lr;
            apply_all_servos();
        }
    } else if (strcmp(action_name, "back_left_wave") == 0) {
        active_animation = 9;
        anim_state = 0;
        anim_pos = 90;
        wait_counter = 0;
    } else if (strcmp(action_name, "back_right_wave") == 0) {
        active_animation = 10;
        anim_state = 0;
        anim_pos = 90;
        wait_counter = 0;
    } else if (strcmp(action_name, "leap_forward") == 0) {
        active_animation = 11;
        anim_state = 0;
        wait_counter = 0;
    } else if (strcmp(action_name, "none") == 0) {
    } else {
        active_animation = 0;
        for (int i = 0; i < 5; i++) {
            if (strcmp(action_name, pose_names[i]) == 0) {
                target_low_left   = poses[i].ll;
                target_high_right = poses[i].hr;
                target_high_left  = poses[i].hl;
                target_low_right  = poses[i].lr;
                break;
            }
        }
        apply_all_servos();
    }
}

void servo_get_angles(int* ll, int* hr, int* hl, int* lr) {
    *ll = target_low_left; *hr = target_high_right;
    *hl = target_high_left; *lr = target_low_right;
}

void servo_get_offsets(int* ll, int* hr, int* hl, int* lr) {
    *ll = offset_low_left; *hr = offset_high_right;
    *hl = offset_high_left; *lr = offset_low_right;
}

void servo_set_calibration(const char* target, int ll, int hr, int hl, int lr, bool save_to_nvs) {
    if (strcmp(target, "offsets") == 0) {
        offset_low_left = ll; offset_high_right = hr;
        offset_high_left = hl; offset_low_right = lr;
        apply_all_servos();
        
        if (save_to_nvs) {
            nvs_handle_t my_handle;
            if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
                nvs_set_i32(my_handle, "ll_off", offset_low_left);
                nvs_set_i32(my_handle, "hr_off", offset_high_right);
                nvs_set_i32(my_handle, "hl_off", offset_high_left);
                nvs_set_i32(my_handle, "lr_off", offset_low_right);
                nvs_commit(my_handle);
                nvs_close(my_handle);
            }
        }
    } else {
        for (int i = 0; i < 5; i++) {
            if (strcmp(target, pose_names[i]) == 0) {
                poses[i].ll = ll; poses[i].hr = hr; poses[i].hl = hl; poses[i].lr = lr;
                servo_set_action(pose_names[i]);
                
                if (save_to_nvs) {
                    nvs_handle_t my_handle;
                    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
                        char key[16];
                        snprintf(key, sizeof(key), "p_%s_ll", pose_keys[i]); nvs_set_i32(my_handle, key, ll);
                        snprintf(key, sizeof(key), "p_%s_hr", pose_keys[i]); nvs_set_i32(my_handle, key, hr);
                        snprintf(key, sizeof(key), "p_%s_hl", pose_keys[i]); nvs_set_i32(my_handle, key, hl);
                        snprintf(key, sizeof(key), "p_%s_lr", pose_keys[i]); nvs_set_i32(my_handle, key, lr);
                        nvs_commit(my_handle);
                        nvs_close(my_handle);
                    }
                }
                break;
            }
        }
    }
}

char* servo_get_calibrations_json() {
    cJSON *root = cJSON_CreateObject();
    
    cJSON *off = cJSON_CreateObject();
    cJSON_AddNumberToObject(off, "ll", offset_low_left);
    cJSON_AddNumberToObject(off, "hr", offset_high_right);
    cJSON_AddNumberToObject(off, "hl", offset_high_left);
    cJSON_AddNumberToObject(off, "lr", offset_low_right);
    cJSON_AddItemToObject(root, "offsets", off);

    for (int i = 0; i < 5; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "ll", poses[i].ll);
        cJSON_AddNumberToObject(p, "hr", poses[i].hr);
        cJSON_AddNumberToObject(p, "hl", poses[i].hl);
        cJSON_AddNumberToObject(p, "lr", poses[i].lr);
        cJSON_AddItemToObject(root, pose_names[i], p);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

char* servo_get_calibrations_cpp() {
    char* dbuf = (char*)malloc(1024);
    snprintf(dbuf, 1024,
        "// =========================================================\n"
        "// ESPRobot Calibration Defaults\n"
        "// =========================================================\n\n"
        "static int32_t offset_low_left   = %ld;\n"
        "static int32_t offset_high_right = %ld;\n"
        "static int32_t offset_high_left  = %ld;\n"
        "static int32_t offset_low_right  = %ld;\n\n"
        "struct Pose { int32_t ll, hr, hl, lr; };\n"
        "static Pose poses[5] = {\n"
        "    {%ld, %ld, %ld, %ld},  // sit\n"
        "    {%ld, %ld, %ld, %ld},  // stand\n"
        "    {%ld, %ld, %ld, %ld},  // stretch_down\n"
        "    {%ld, %ld, %ld, %ld},  // stretch_back\n"
        "    {%ld, %ld, %ld, %ld}   // stop\n"
        "};\n",
        offset_low_left, offset_high_right, offset_high_left, offset_low_right,
        poses[0].ll, poses[0].hr, poses[0].hl, poses[0].lr,
        poses[1].ll, poses[1].hr, poses[1].hl, poses[1].lr,
        poses[2].ll, poses[2].hr, poses[2].hl, poses[2].lr,
        poses[3].ll, poses[3].hr, poses[3].hl, poses[3].lr,
        poses[4].ll, poses[4].hr, poses[4].hl, poses[4].lr
    );
    return dbuf;
}