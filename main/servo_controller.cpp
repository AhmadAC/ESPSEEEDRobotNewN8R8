// main\servo_controller.cpp (Facade)
#include "servo_controller.h"
#include "servo_hw.h"
#include "servo_storage.h"
#include "servo_animation.h"
#include "sensor_monitor.h"
#include <string.h>

void servo_controller_init(void) {
    servo_storage_init();
    servo_hw_init();
    servo_animation_init();
}

void servo_set_target(const char *id, int angle) {
    if (sensor_is_safety_locked()) return;
    servo_hw_set_active(true);
    servo_animation_stop();

    servo_storage_set_target_by_id(id, angle);
    int o_ll, o_hr, o_hl, o_lr;
    servo_storage_get_offsets(&o_ll, &o_hr, &o_hl, &o_lr);

    if (strcmp(id, "low_left") == 0) {
        servo_hw_write(SERVO_LOW_LEFT_CH, 180 - angle, -o_ll);
        servo_hw_schedule_relax();
    } else if (strcmp(id, "high_right") == 0) {
        servo_hw_write(SERVO_HIGH_RIGHT_CH, angle, o_hr);
        servo_hw_schedule_relax();
    } else if (strcmp(id, "high_left") == 0) {
        servo_hw_write(SERVO_HIGH_LEFT_CH, 180 - angle, -o_hl);
        servo_hw_schedule_relax();
    } else if (strcmp(id, "low_right") == 0) {
        servo_hw_write(SERVO_LOW_RIGHT_CH, angle, o_lr);
        servo_hw_schedule_relax();
    } else if (strcmp(id, "all") == 0) {
        servo_animation_apply_staggered();
    }
}

void servo_set_target_silent(const char *id, int angle) {
    if (sensor_is_safety_locked()) return;
    servo_hw_set_active(true);
    servo_animation_stop();
    servo_storage_set_target_by_id(id, angle);
}

void servo_apply_targets(void) {
    if (sensor_is_safety_locked()) return;
    servo_hw_set_active(true);
    servo_animation_apply_staggered();
}

void servo_set_action(const char *action_name) {
    if (sensor_is_safety_locked()) return;
    servo_set_action_bypass(action_name);
}

void servo_set_action_bypass(const char *action_name) {
    if (strcmp(action_name, "stop") == 0) {
        servo_animation_stop();
        servo_hw_relax_all();
        return;
    }
    if (strcmp(action_name, "none") == 0) return;

    servo_hw_set_active(true);

    if (strcmp(action_name, "forward") == 0)        servo_animation_start(1);
    else if (strcmp(action_name, "backward") == 0)  servo_animation_start(2);
    else if (strcmp(action_name, "step_forward") == 0)  servo_animation_start(3);
    else if (strcmp(action_name, "step_backward") == 0) servo_animation_start(4);
    else if (strcmp(action_name, "left_wave") == 0)     servo_animation_start(5);
    else if (strcmp(action_name, "right_wave") == 0)    servo_animation_start(6);
    else if (strcmp(action_name, "crawl") == 0)         servo_animation_start(7);
    else if (strcmp(action_name, "stand") == 0)         servo_animation_start(8);
    else if (strcmp(action_name, "back_left_wave") == 0)  servo_animation_start(9);
    else if (strcmp(action_name, "back_right_wave") == 0) servo_animation_start(10);
    else if (strcmp(action_name, "leap_forward") == 0)  servo_animation_start(11);
    else if (strcmp(action_name, "sit") == 0)           servo_animation_start(12);
    else if (strcmp(action_name, "stretch_down") == 0)  servo_animation_start(13);
    else if (strcmp(action_name, "stretch_back") == 0)  servo_animation_start(14);
    else {
        int idx = servo_storage_find_pose_index(action_name);
        if (idx >= 0) {
            ServoPose p = servo_storage_get_pose(idx);
            servo_storage_set_targets(p.ll, p.hr, p.hl, p.lr);
            servo_animation_apply_staggered();
        }
    }
}

void servo_get_angles(int *ll, int *hr, int *hl, int *lr) {
    servo_storage_get_targets(ll, hr, hl, lr);
}

void servo_get_offsets(int *ll, int *hr, int *hl, int *lr) {
    servo_storage_get_offsets(ll, hr, hl, lr);
}

void servo_set_pins(int ll, int hr, int hl, int lr) {
    servo_hw_set_pins(ll, hr, hl, lr);
}

void servo_get_pins(int *ll, int *hr, int *hl, int *lr) {
    servo_hw_get_pins(ll, hr, hl, lr);
}

void servo_set_leg_names(const char *hl, const char *hr, const char *ll, const char *lr) {
    servo_storage_set_leg_names(hl, hr, ll, lr);
}

void servo_get_leg_names(char *hl, char *hr, char *ll, char *lr, size_t max_len) {
    servo_storage_get_leg_names(hl, hr, ll, lr, max_len);
}

char* servo_get_calibrations_json(void) {
    return servo_storage_get_json();
}

char* servo_get_calibrations_cpp(void) {
    return servo_storage_get_cpp();
}

void servo_set_calibration(const char *target, int ll, int hr, int hl, int lr, bool save_to_nvs) {
    servo_hw_set_active(true);
    if (strcmp(target, "offsets") == 0) {
        servo_storage_set_offsets(ll, hr, hl, lr, save_to_nvs);
        servo_animation_apply_staggered();
    } else {
        int idx = servo_storage_find_pose_index(target);
        if (idx >= 0) {
            servo_storage_set_pose(idx, ll, hr, hl, lr, save_to_nvs);
            servo_set_action(servo_storage_get_pose_name(idx));
        }
    }
}