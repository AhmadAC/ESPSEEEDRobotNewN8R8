
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
        vTaskDelay(pdMS_TO_TICKS(18));
    }
    if (s_app_hr != t_hr) {
        servo_hw_write(SERVO_HIGH_RIGHT_CH, t_hr, o_hr);
        s_app_hr = t_hr;
        vTaskDelay(pdMS_TO_TICKS(18));
    }
    if (s_app_ll != t_ll) {
        servo_hw_write(SERVO_LOW_LEFT_CH, 180 - t_ll, -o_ll);
        s_app_ll = t_ll;
        vTaskDelay(pdMS_TO_TICKS(18));
    }
    if (s_app_lr != t_lr) {
        servo_hw_write(SERVO_LOW_RIGHT_CH, t_lr, o_lr);
        s_app_lr = t_lr;
    }
    servo_hw_schedule_relax();
}

static void run_wave(ledc_channel_t ch, int *state, int *pos, bool inv, int off) {
    const int step = 12;
    if (*state % 2 == 0) {
        *pos += step;
        if (*pos >= 170) { *pos = 170; (*state)++; }
    } else {
        *pos -= step;
        if (*pos <= 10) { *pos = 10; (*state)++; }
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

            int target_hl = 90, target_lr = 90;
            int target_hr = 90, target_ll = 90;

            if (s_anim_state == 0) {
                target_hl = 90 + (SWING_MAG * dir);
                target_lr = 90 + (SWING_MAG * dir);
                target_hr = 90 - (SWING_MAG * dir);
                target_ll = 90 - (SWING_MAG * dir);
            } else if (s_anim_state == 1) {
                target_hl = 90; target_lr = 90; target_hr = 90; target_ll = 90;
            } else if (s_anim_state == 2) {
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
        } 
        // -----------------------------------------------------------------
        // BROWNOUT-FREE 4-LEG CRAWL GAIT (ANIM 7)
        // Low-profile, crouched crawling gait with gentle 4 deg ramp steps
        // -----------------------------------------------------------------
        else if (s_active_anim == 7) {
            servo_hw_cancel_relax();
            const int CROUCH_BASE = 65;
            const int CRAWL_SWING = 20;

            int target_hl = CROUCH_BASE, target_lr = CROUCH_BASE;
            int target_hr = CROUCH_BASE, target_ll = CROUCH_BASE;

            if (s_anim_state == 0) {
                // Diagonal Pair A steps forward, Pair B pushes backward
                target_hl = CROUCH_BASE + CRAWL_SWING;
                target_lr = CROUCH_BASE + CRAWL_SWING;
                target_hr = CROUCH_BASE - CRAWL_SWING;
                target_ll = CROUCH_BASE - CRAWL_SWING;
            } else if (s_anim_state == 1) {
                // Return to neutral crouch
                target_hl = CROUCH_BASE; target_lr = CROUCH_BASE;
                target_hr = CROUCH_BASE; target_ll = CROUCH_BASE;
            } else if (s_anim_state == 2) {
                // Diagonal Pair B steps forward, Pair A pushes backward
                target_hl = CROUCH_BASE - CRAWL_SWING;
                target_lr = CROUCH_BASE - CRAWL_SWING;
                target_hr = CROUCH_BASE + CRAWL_SWING;
                target_ll = CROUCH_BASE + CRAWL_SWING;
            } else {
                target_hl = CROUCH_BASE; target_lr = CROUCH_BASE;
                target_hr = CROUCH_BASE; target_ll = CROUCH_BASE;
            }

            // Step with gentle 4 deg/step and 8ms phase stagger to prevent rail dip
            bool d_hl = servo_hw_step_towards(SERVO_HIGH_LEFT_CH, 180 - target_hl, -o_hl, 4);
            bool d_lr = servo_hw_step_towards(SERVO_LOW_RIGHT_CH, target_lr, o_lr, 4);
            vTaskDelay(pdMS_TO_TICKS(8));

            bool d_hr = servo_hw_step_towards(SERVO_HIGH_RIGHT_CH, target_hr, o_hr, 4);
            bool d_ll = servo_hw_step_towards(SERVO_LOW_LEFT_CH, 180 - target_ll, -o_ll, 4);
            vTaskDelay(pdMS_TO_TICKS(8));

            if (d_hl && d_lr && d_hr && d_ll) {
                s_anim_state = (s_anim_state + 1) % 4;
            }
        } else if (s_active_anim == 8) {
            // STAND (Index 1) - Gentle 3 deg/step to eliminate battery brownout
            anim_step_concurrent(1, 3);
        } else if (s_active_anim == 9) {
            run_wave(SERVO_LOW_LEFT_CH, &s_anim_state, &s_anim_pos, true, -o_ll);
        } else if (s_active_anim == 10) {
            run_wave(SERVO_LOW_RIGHT_CH, &s_anim_state, &s_anim_pos, false, o_lr);
        } 
        // -----------------------------------------------------------------
        // BROWNOUT-FREE FORWARD LEAP / POUNCE (ANIM 11)
        // 4-Phase physics: Pre-load crouch -> Spring thrust -> Cushion -> Stand
        // -----------------------------------------------------------------
        else if (s_active_anim == 11) {
            servo_hw_cancel_relax();

            if (s_anim_state == 0) {
                // Phase 0: Crouch down smoothly to store energy
                bool d_hl = servo_hw_step_towards(SERVO_HIGH_LEFT_CH, 180 - 60, -o_hl, 5);
                bool d_hr = servo_hw_step_towards(SERVO_HIGH_RIGHT_CH, 60, o_hr, 5);
                vTaskDelay(pdMS_TO_TICKS(8));

                bool d_ll = servo_hw_step_towards(SERVO_LOW_LEFT_CH, 180 - 60, -o_ll, 5);
                bool d_lr = servo_hw_step_towards(SERVO_LOW_RIGHT_CH, 60, o_lr, 5);
                vTaskDelay(pdMS_TO_TICKS(8));

                if (d_hl && d_hr && d_ll && d_lr) {
                    s_wait_counter = 0;
                    s_anim_state = 1;
                }
            } else if (s_anim_state == 1) {
                // Phase 1: Brief pause in crouch
                if (++s_wait_counter >= 5) {
                    s_anim_state = 2;
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            } else if (s_anim_state == 2) {
                // Phase 2: Forward launch thrust (Front reaches 120, Rear pushes back to 125)
                bool d_hl = servo_hw_step_towards(SERVO_HIGH_LEFT_CH, 180 - 120, -o_hl, 8);
                bool d_hr = servo_hw_step_towards(SERVO_HIGH_RIGHT_CH, 120, o_hr, 8);
                vTaskDelay(pdMS_TO_TICKS(8));

                bool d_ll = servo_hw_step_towards(SERVO_LOW_LEFT_CH, 180 - 125, -o_ll, 8);
                bool d_lr = servo_hw_step_towards(SERVO_LOW_RIGHT_CH, 125, o_lr, 8);
                vTaskDelay(pdMS_TO_TICKS(8));

                if (d_hl && d_hr && d_ll && d_lr) {
                    s_anim_state = 3;
                }
            } else if (s_anim_state == 3) {
                // Phase 3: Land and cushion impact
                bool d_hl = servo_hw_step_towards(SERVO_HIGH_LEFT_CH, 180 - 85, -o_hl, 6);
                bool d_hr = servo_hw_step_towards(SERVO_HIGH_RIGHT_CH, 85, o_hr, 6);
                vTaskDelay(pdMS_TO_TICKS(8));

                bool d_ll = servo_hw_step_towards(SERVO_LOW_LEFT_CH, 180 - 95, -o_ll, 6);
                bool d_lr = servo_hw_step_towards(SERVO_LOW_RIGHT_CH, 95, o_lr, 6);
                vTaskDelay(pdMS_TO_TICKS(8));

                if (d_hl && d_hr && d_ll && d_lr) {
                    s_anim_state = 4;
                }
            } else if (s_anim_state == 4) {
                // Phase 4: Recover to 90 deg Stand pose smoothly
                bool d_hl = servo_hw_step_towards(SERVO_HIGH_LEFT_CH, 180 - 90, -o_hl, 4);
                bool d_hr = servo_hw_step_towards(SERVO_HIGH_RIGHT_CH, 90, o_hr, 4);
                vTaskDelay(pdMS_TO_TICKS(8));

                bool d_ll = servo_hw_step_towards(SERVO_LOW_LEFT_CH, 180 - 90, -o_ll, 4);
                bool d_lr = servo_hw_step_towards(SERVO_LOW_RIGHT_CH, 90, o_lr, 4);
                vTaskDelay(pdMS_TO_TICKS(8));

                if (d_hl && d_hr && d_ll && d_lr) {
                    s_active_anim = 0;
                    servo_hw_schedule_relax();
                }
            }
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