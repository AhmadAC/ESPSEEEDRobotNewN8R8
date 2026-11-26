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

    // Use smooth moves with interpolation (5 degrees per step, 30ms delay)
    if (s_app_ll != t_ll) {
        servo_hw_smooth_write(SERVO_LOW_LEFT_CH, 180 - t_ll, -o_ll, &current_angle_ll, 5, 30);
        s_app_ll = t_ll;
        vTaskDelay(pdMS_TO_TICKS(50)); // Small pause between servos
    }
    if (s_app_hr != t_hr) {
        servo_hw_smooth_write(SERVO_HIGH_RIGHT_CH, t_hr, o_hr, &current_angle_hr, 5, 30);
        s_app_hr = t_hr;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_app_hl != t_hl) {
        servo_hw_smooth_write(SERVO_HIGH_LEFT_CH, 180 - t_hl, -o_hl, &current_angle_hl, 5, 30);
        s_app_hl = t_hl;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_app_lr != t_lr) {
        servo_hw_smooth_write(SERVO_LOW_RIGHT_CH, t_lr, o_lr, &current_angle_lr, 5, 30);
        s_app_lr = t_lr;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    servo_hw_schedule_relax();
}

// ... rest of the animation functions remain the same, but you can optionally
// replace direct servo_hw_write calls with servo_hw_smooth_write for smoother animations

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
        // Use smooth move for first leg
        servo_hw_smooth_write(SERVO_LOW_LEFT_CH, 180 - p.ll, -o_ll, &current_angle_ll, 5, 30);
        s_wait_counter = 0; s_anim_state = 1;
    } else if (s_anim_state == 1 && ++s_wait_counter >= 9) {
        servo_hw_smooth_write(SERVO_LOW_RIGHT_CH, p.lr, o_lr, &current_angle_lr, 5, 30);
        s_wait_counter = 0; s_anim_state = 2;
    } else if (s_anim_state == 2 && ++s_wait_counter >= 9) {
        servo_hw_smooth_write(SERVO_HIGH_LEFT_CH, 180 - p.hl, -o_hl, &current_angle_hl, 5, 30);
        s_wait_counter = 0; s_anim_state = 3;
    } else if (s_anim_state == 3 && ++s_wait_counter >= 9) {
        servo_hw_smooth_write(SERVO_HIGH_RIGHT_CH, p.hr, o_hr, &current_angle_hr, 5, 30);
        s_wait_counter = 0; s_anim_state = 4;
    } else if (s_anim_state == 4 && ++s_wait_counter >= 9) {
        s_active_anim = 0; servo_hw_schedule_relax();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
}

// ... rest of servo_animation_task and other functions remain the same

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
