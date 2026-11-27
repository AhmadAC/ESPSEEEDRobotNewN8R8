

#include "servo_animation.h"
#include "servo_hw.h"
#include "servo_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "SERVO_ANIM";

static int s_active_anim = 0;
static int s_anim_state = 0;
static int s_anim_pos = 90;
static int s_wait_counter = 0;

static int32_t s_app_ll = -1, s_app_hr = -1, s_app_hl = -1, s_app_lr = -1;

void servo_animation_apply_staggered(void) {
    if (!servo_hw_is_active()) return;
    int t_ll, t_hr, t_hl, t_lr;
    servo_storage_get_targets(&t_ll, &t_hr, &t_hl, &t_lr);
    int o_ll, o_hr, o_hl, o_lr;
    servo_storage_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);

    if (s_app_hl != t_hl) {
        servo_hw_write(SERVO_HIGH_LEFT_CH, 180 - t_hl, -o_hl);
        s_app_hl = t_hl;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_app_hr != t_hr) {
        servo_hw_write(SERVO_HIGH_RIGHT_CH, t_hr, o_hr);
        s_app_hr = t_hr;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_app_ll != t_ll) {
        servo_hw_write(SERVO_LOW_LEFT_CH, 180 - t_ll, -o_ll);
        s_app_ll = t_ll;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_app_lr != t_lr) {
        servo_hw_write(SERVO_LOW_RIGHT_CH, t_lr, o_lr);
        s_app_lr = t_lr;
    }
    servo_hw_schedule_relax();
}

static void run_wave(ledc_channel_t ch, int *state, int *pos, bool inv, int off) {
    const int step = 20;
    if (*state % 2 == 0) {
        *pos += step;
        if (*pos >= 180) { *pos = 180; (*state)++; }
    } else {
        *pos -= step;
        if (*pos <= 0) { *pos = 0; (*state)++; }
    }
    if (*state >= 6 && *pos >= 90) {
        *pos = 90; s_active_anim = 0; servo_hw_schedule_relax();
    }
    servo_hw_write(ch, inv ? (180 - *pos) : *pos, off);
    vTaskDelay(pdMS_TO_TICKS(20));
}

// Low-current concurrent stepping: staggers front vs rear pairs by 10ms with gentle step
static void anim_step_concurrent(int p_idx, int step_size) {
    ServoPose p = servo_storage_get_pose(p_idx);
    int o_ll, o_hr, o_hl, o_lr;
    servo_storage_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);

    servo_storage_set_targets(p.ll, p.hr, p.hl, p.lr);

    bool d_hl = servo_hw_step_towards(SERVO_HIGH_LEFT_CH, 180 - p.hl, -o_hl, step_size);
    bool d_hr = servo_hw_step_towards(SERVO_HIGH_RIGHT_CH, p.hr, o_hr, step_size);
    vTaskDelay(pdMS_TO_TICKS(10));

    bool d_ll = servo_hw_step_towards(SERVO_LOW_LEFT_CH, 180 - p.ll, -o_ll, step_size);
    bool d_lr = servo_hw_step_towards(SERVO_LOW_RIGHT_CH, p.lr, o_lr, step_size);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (d_ll && d_hr && d_hl && d_lr) {
        s_active_anim = 0;
        servo_hw_schedule_relax();
    }
}

static void servo_animation_task(void *pv) {
    while (1) {
        if (servo_hw_check_and_clear_relax_req()) {
            servo_hw_relax_all();
            s_app_ll = s_app_hr = s_app_hl = s_app_lr = -1;
            ESP_LOGI(TAG, "Servos relaxed (0 Amps draw)");
        }
        if (!servo_hw_is_active()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int o_ll, o_hr, o_hl, o_lr;
        servo_storage_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);

        // -----------------------------------------------------------------
        // FAST, FLUID QUADRUPED TROT GAIT (FORWARD: 1, BACKWARD: 2)
        // Diagonal Pairs: Pair A (HL, LR), Pair B (HR, LL)
        // -----------------------------------------------------------------
        if (s_active_anim == 1 || s_active_anim == 2) {
            servo_hw_cancel_relax();
            const int SWING_MAG = 28;
            int dir = (s_active_anim == 1) ? 1 : -1;

            // Target angles for 4 phases of diagonal trot
            int target_hl = 90, target_lr = 90;
            int target_hr = 90, target_ll = 90;

            if (s_anim_state == 0) {
                // Phase 0: Pair A swings forward, Pair B pushes backward
                target_hl = 90 + (SWING_MAG * dir);
                target_lr = 90 + (SWING_MAG * dir);
                target_hr = 90 - (SWING_MAG * dir);
                target_ll = 90 - (SWING_MAG * dir);
            } else if (s_anim_state == 1) {
                // Phase 1: Neutral plant crossing
                target_hl = 90;
                target_lr = 90;
                target_hr = 90;
                target_ll = 90;
            } else if (s_anim_state == 2) {
                // Phase 2: Pair B swings forward, Pair A pushes backward
                target_hl = 90 - (SWING_MAG * dir);
                target_lr = 90 - (SWING_MAG * dir);
                target_hr = 90 + (SWING_MAG * dir);
                target_ll = 90 + (SWING_MAG * dir);
            } else {
                // Phase 3: Neutral plant crossing
                target_hl = 90;
                target_lr = 90;
                target_hr = 90;
                target_ll = 90;
            }

            // Rapid step (8 deg/step) with 8ms phase stagger to prevent brownout
            bool d_hl = servo_hw_step_towards(SERVO_HIGH_LEFT_CH, 180 - target_hl, -o_hl, 8);
            bool d_lr = servo_hw_step_towards(SERVO_LOW_RIGHT_CH, target_lr, o_lr, 8);
            vTaskDelay(pdMS_TO_TICKS(8));

            bool d_hr = servo_hw_step_towards(SERVO_HIGH_RIGHT_CH, target_hr, o_hr, 8);
            bool d_ll = servo_hw_step_towards(SERVO_LOW_LEFT_CH, 180 - target_ll, -o_ll, 8);
            vTaskDelay(pdMS_TO_TICKS(8));

            if (d_hl && d_lr && d_hr && d_ll) {
                s_anim_state = (s_anim_state + 1) % 4;
            }
        } 
        // -----------------------------------------------------------------
        // SINGLE STEP FORWARD (3) OR BACKWARD (4)
        // -----------------------------------------------------------------
        else if (s_active_anim == 3 || s_active_anim == 4) {
            servo_hw_cancel_relax();
            const int SWING_MAG = 25;
            int dir = (s_active_anim == 3) ? 1 : -1;

            int target_hl = 90, target_lr = 90;
            int target_hr = 90, target_ll = 90;

            if (s_anim_state == 0) {
                target_hl = 90 + (SWING_MAG * dir);
                target_lr = 90 + (SWING_MAG * dir);
                target_hr = 90 - (SWING_MAG * dir);
                target_ll = 90 - (SWING_MAG * dir);
            } else if (s_anim_state == 1) {
                target_hl = 90 - (SWING_MAG * dir);
                target_lr = 90 - (SWING_MAG * dir);
                target_hr = 90 + (SWING_MAG * dir);
                target_ll = 90 + (SWING_MAG * dir);
            } else {
                target_hl = 90; target_lr = 90; target_hr = 90; target_ll = 90;
            }

            bool d_hl = servo_hw_step_towards(SERVO_HIGH_LEFT_CH, 180 - target_hl, -o_hl, 8);
            bool d_lr = servo_hw_step_towards(SERVO_LOW_RIGHT_CH, target_lr, o_lr, 8);
            vTaskDelay(pdMS_TO_TICKS(8));

            bool d_hr = servo_hw_step_towards(SERVO_HIGH_RIGHT_CH, target_hr, o_hr, 8);
            bool d_ll = servo_hw_step_towards(SERVO_LOW_LEFT_CH, 180 - target_ll, -o_ll, 8);
            vTaskDelay(pdMS_TO_TICKS(8));

            if (d_hl && d_lr && d_hr && d_ll) {
                s_anim_state++;
                if (s_anim_state >= 3) {
                    s_active_anim = 0;
                    servo_hw_schedule_relax();
                }
            }
        } else if (s_active_anim == 5) {
            run_wave(SERVO_HIGH_LEFT_CH, &s_anim_state, &s_anim_pos, true, -o_hl);
        } else if (s_active_anim == 6) {
            run_wave(SERVO_HIGH_RIGHT_CH, &s_anim_state, &s_anim_pos, false, o_hr);
        } else if (s_active_anim == 7) {
            servo_hw_cancel_relax();
            s_anim_pos += (s_anim_state == 0) ? 6 : -6;
            if (s_anim_pos >= 135) s_anim_state = 1;
            if (s_anim_pos <= 45)  s_anim_state = 0;
            servo_hw_write(SERVO_HIGH_LEFT_CH, 180 - s_anim_pos, -o_hl); vTaskDelay(pdMS_TO_TICKS(15));
            servo_hw_write(SERVO_HIGH_RIGHT_CH, s_anim_pos, o_hr); vTaskDelay(pdMS_TO_TICKS(15));
        } else if (s_active_anim == 8) {
            // STAND (Index 1) - Gentle 3 deg/step to eliminate battery brownout
            anim_step_concurrent(1, 3);
        } else if (s_active_anim == 9) {
            run_wave(SERVO_LOW_LEFT_CH, &s_anim_state, &s_anim_pos, true, -o_ll);
        } else if (s_active_anim == 10) {
            run_wave(SERVO_LOW_RIGHT_CH, &s_anim_state, &s_anim_pos, false, o_lr);
        } else if (s_active_anim == 11) {
            servo_hw_cancel_relax();
            if (s_anim_state == 0) {
                servo_hw_write(SERVO_HIGH_LEFT_CH, 0, -o_hl); s_anim_state = 1;
            } else if (s_anim_state == 1) {
                servo_hw_write(SERVO_HIGH_RIGHT_CH, 180, o_hr); s_wait_counter = 0; s_anim_state = 2;
            } else if (s_anim_state == 2 && ++s_wait_counter >= 8) {
                servo_hw_write(SERVO_LOW_LEFT_CH, 0, -o_ll); s_anim_state = 3;
            } else if (s_anim_state == 3) {
                servo_hw_write(SERVO_LOW_RIGHT_CH, 180, o_lr); s_wait_counter = 0; s_anim_state = 4;
            } else if (s_anim_state == 4 && ++s_wait_counter >= 15) {
                servo_hw_write(SERVO_LOW_LEFT_CH, 180, -o_ll); s_anim_state = 5;
            } else if (s_anim_state == 5) {
                servo_hw_write(SERVO_LOW_RIGHT_CH, 0, o_lr); s_wait_counter = 0; s_anim_state = 6;
            } else if (s_anim_state == 6 && ++s_wait_counter >= 20) {
                s_anim_state = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (s_active_anim == 12) {
            // SIT (Index 0) - Gentle 3 deg/step to eliminate battery brownout
            anim_step_concurrent(0, 3);
        } else if (s_active_anim == 13) {
            // STRETCH DOWN (Index 2)
            anim_step_concurrent(2, 3);
        } else if (s_active_anim == 14) {
            // STRETCH BACK (Index 3)
            anim_step_concurrent(3, 3);
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void servo_animation_init(void) {
    xTaskCreatePinnedToCore(servo_animation_task, "anim_tsk", 4096, NULL, 5, NULL, 1);
}

void servo_animation_start(int anim_id) {
    s_active_anim = anim_id;
    s_anim_state = 0;
    s_anim_pos = 90;
    s_wait_counter = 0;
    servo_hw_set_active(true);
}

void servo_animation_stop(void) {
    s_active_anim = 0;
    s_anim_state = 0;
}

int servo_animation_get_active(void) {
    return s_active_anim;
}