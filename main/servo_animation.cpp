// main\servo_animation.cpp
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

    if (s_app_ll != t_ll) {
        servo_hw_write(SERVO_LOW_LEFT_CH, 180 - t_ll, -o_ll);
        s_app_ll = t_ll; vTaskDelay(pdMS_TO_TICKS(140));
    }
    if (s_app_hr != t_hr) {
        servo_hw_write(SERVO_HIGH_RIGHT_CH, t_hr, o_hr);
        s_app_hr = t_hr; vTaskDelay(pdMS_TO_TICKS(140));
    }
    if (s_app_hl != t_hl) {
        servo_hw_write(SERVO_HIGH_LEFT_CH, 180 - t_hl, -o_hl);
        s_app_hl = t_hl; vTaskDelay(pdMS_TO_TICKS(140));
    }
    if (s_app_lr != t_lr) {
        servo_hw_write(SERVO_LOW_RIGHT_CH, t_lr, o_lr);
        s_app_lr = t_lr; vTaskDelay(pdMS_TO_TICKS(140));
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

static void anim_step_staggered(int p_idx) {
    ServoPose p = servo_storage_get_pose(p_idx);
    int o_ll, o_hr, o_hl, o_lr;
    servo_storage_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);

    if (s_anim_state == 0) {
        servo_storage_set_targets(p.ll, p.hr, p.hl, p.lr);
        servo_hw_write(SERVO_LOW_LEFT_CH, 180 - p.ll, -o_ll);
        s_wait_counter = 0; s_anim_state = 1;
    } else if (s_anim_state == 1 && ++s_wait_counter >= 9) {
        servo_hw_write(SERVO_LOW_RIGHT_CH, p.lr, o_lr);
        s_wait_counter = 0; s_anim_state = 2;
    } else if (s_anim_state == 2 && ++s_wait_counter >= 9) {
        servo_hw_write(SERVO_HIGH_LEFT_CH, 180 - p.hl, -o_hl);
        s_wait_counter = 0; s_anim_state = 3;
    } else if (s_anim_state == 3 && ++s_wait_counter >= 9) {
        servo_hw_write(SERVO_HIGH_RIGHT_CH, p.hr, o_hr);
        s_wait_counter = 0; s_anim_state = 4;
    } else if (s_anim_state == 4 && ++s_wait_counter >= 9) {
        s_active_anim = 0; servo_hw_schedule_relax();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
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

        if (s_active_anim == 1 || s_active_anim == 2) {
            servo_hw_cancel_relax();
            int dir = (s_active_anim == 1) ? 1 : -1;
            if (s_anim_state == 0) {
                s_anim_pos += 4 * dir;
                if (s_anim_pos >= 130 || s_anim_pos <= 50) s_anim_state = 1;
            } else {
                s_anim_pos -= 4 * dir;
                if (s_anim_pos <= 50 || s_anim_pos >= 130) s_anim_state = 0;
            }
            servo_hw_write(SERVO_HIGH_LEFT_CH, 180 - s_anim_pos, -o_hl); vTaskDelay(pdMS_TO_TICKS(25));
            servo_hw_write(SERVO_LOW_RIGHT_CH, s_anim_pos, o_lr); vTaskDelay(pdMS_TO_TICKS(25));
            servo_hw_write(SERVO_HIGH_RIGHT_CH, 180 - s_anim_pos, o_hr); vTaskDelay(pdMS_TO_TICKS(25));
            servo_hw_write(SERVO_LOW_LEFT_CH, 180 - s_anim_pos, -o_ll); vTaskDelay(pdMS_TO_TICKS(25));
        } else if (s_active_anim == 3 || s_active_anim == 4) {
            int dir = (s_active_anim == 3) ? 1 : -1;
            if (s_anim_state == 0) {
                s_anim_pos += 4 * dir;
                if (s_anim_pos >= 130 || s_anim_pos <= 50) s_anim_state = 1;
            } else if (s_anim_state == 1) {
                s_anim_pos -= 4 * dir;
                if (s_anim_pos <= 50 || s_anim_pos >= 130) s_anim_state = 2;
            } else if (s_anim_state == 2) {
                s_anim_pos += 4 * dir;
                if ((dir > 0 && s_anim_pos >= 90) || (dir < 0 && s_anim_pos <= 90)) {
                    s_anim_pos = 90; s_active_anim = 0; servo_hw_schedule_relax();
                }
            }
            servo_hw_write(SERVO_HIGH_LEFT_CH, 180 - s_anim_pos, -o_hl); vTaskDelay(pdMS_TO_TICKS(20));
            servo_hw_write(SERVO_LOW_RIGHT_CH, s_anim_pos, o_lr); vTaskDelay(pdMS_TO_TICKS(20));
            servo_hw_write(SERVO_HIGH_RIGHT_CH, 180 - s_anim_pos, o_hr); vTaskDelay(pdMS_TO_TICKS(20));
            servo_hw_write(SERVO_LOW_LEFT_CH, 180 - s_anim_pos, -o_ll); vTaskDelay(pdMS_TO_TICKS(20));
        } else if (s_active_anim == 5) {
            run_wave(SERVO_HIGH_LEFT_CH, &s_anim_state, &s_anim_pos, true, -o_hl);
        } else if (s_active_anim == 6) {
            run_wave(SERVO_HIGH_RIGHT_CH, &s_anim_state, &s_anim_pos, false, o_hr);
        } else if (s_active_anim == 7) {
            servo_hw_cancel_relax();
            s_anim_pos += (s_anim_state == 0) ? 5 : -5;
            if (s_anim_pos >= 135) s_anim_state = 1;
            if (s_anim_pos <= 45)  s_anim_state = 0;
            servo_hw_write(SERVO_HIGH_LEFT_CH, 180 - s_anim_pos, -o_hl); vTaskDelay(pdMS_TO_TICKS(20));
            servo_hw_write(SERVO_HIGH_RIGHT_CH, s_anim_pos, o_hr); vTaskDelay(pdMS_TO_TICKS(20));
        } else if (s_active_anim == 8) {
            anim_step_staggered(1); // stand
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
            anim_step_staggered(0); // sit
        } else if (s_active_anim == 13) {
            anim_step_staggered(2); // stretch_down
        } else if (s_active_anim == 14) {
            anim_step_staggered(3); // stretch_back
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