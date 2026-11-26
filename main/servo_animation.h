#pragma once
#include <stdint.h>
#include <stdbool.h>

void servo_animation_init(void);
void servo_animation_start(int anim_id);
void servo_animation_stop(void);
int  servo_animation_get_active(void);
void servo_animation_apply_staggered(void);