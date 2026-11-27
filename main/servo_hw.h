#pragma once
#include "driver/ledc.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Channel definitions
#define SERVO_LOW_LEFT_CH   LEDC_CHANNEL_0
#define SERVO_HIGH_RIGHT_CH LEDC_CHANNEL_1
#define SERVO_HIGH_LEFT_CH  LEDC_CHANNEL_2
#define SERVO_LOW_RIGHT_CH  LEDC_CHANNEL_3

void servo_hw_init(void);
void servo_hw_set_pins(int ll, int hr, int hl, int lr);
void servo_hw_get_pins(int *ll, int *hr, int *hl, int *lr);

// Instant move (used for fast gait cycles)
void servo_hw_write(ledc_channel_t channel, int32_t target_angle, int32_t offset);

// Smooth move (blocking single servo interpolation)
void servo_hw_smooth_write(ledc_channel_t channel, int32_t target_angle, int32_t offset, int step_size, int delay_ms);

// Concurrent step towards target (non-blocking step per tick; returns true when reached)
bool servo_hw_step_towards(ledc_channel_t channel, int32_t target_angle, int32_t offset, int32_t step);

void servo_hw_relax_all(void);
void servo_hw_schedule_relax(void);
void servo_hw_cancel_relax(void);
bool servo_hw_check_and_clear_relax_req(void);

bool servo_hw_is_active(void);
void servo_hw_set_active(bool active);

#ifdef __cplusplus
}
#endif