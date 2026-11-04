// main/claw_controller.cpp
#include "claw_controller.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CLAW";
char claw_last_command_str[32] = "INITIALIZED";
int claw_current_angle = 90;

// Hardware Pin Definition (ESP32-S3 Zero to MG90S Signal Wire)
#define SERVO_PIN (GPIO_NUM_1) // D0

// Servo Constants
#define SERVO_MIN_PULSEWIDTH_US 500  
#define SERVO_MAX_PULSEWIDTH_US 2500 
#define SERVO_MAX_DEGREE        180  

static TimerHandle_t claw_relax_timer = NULL;
static bool claw_channel_attached = false;

// Attach PWM hardware channel to pin only when moving
static void attach_claw_channel() {
    if (claw_channel_attached) return;

    ledc_channel_config_t ledc_channel = {};
    ledc_channel.speed_mode     = LEDC_LOW_SPEED_MODE;
    ledc_channel.channel        = LEDC_CHANNEL_1;
    ledc_channel.timer_sel      = LEDC_TIMER_1;
    ledc_channel.intr_type      = LEDC_INTR_DISABLE;
    ledc_channel.gpio_num       = SERVO_PIN;
    ledc_channel.duty           = 0; 
    ledc_channel.hpoint         = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    claw_channel_attached = true;
}

// Detach PWM channel and clamp pin firmly to 0V ground with internal pull-down
static void detach_claw_channel() {
    if (!claw_channel_attached) return;

    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << SERVO_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(SERVO_PIN, 0);

    claw_channel_attached = false;
    ESP_LOGI(TAG, "Claw pin detached and clamped to 0V (0 Amps draw)");
}

// Auto-Relax Callback: Cuts power to the servo to prevent heating/stalling
static void claw_relax_cb(TimerHandle_t xTimer) {
    detach_claw_channel();
    ESP_LOGI(TAG, "Claw Auto-Relaxed (0 Amps draw) to prevent overheating.");
}

void claw_controller_init() {
    // 1. Initial 0V Clamp on Pin
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << SERVO_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(SERVO_PIN, 0);

    // 2. Configure Timer (channel remains detached until commanded)
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode       = LEDC_LOW_SPEED_MODE;
    ledc_timer.timer_num        = LEDC_TIMER_1;
    ledc_timer.duty_resolution  = LEDC_TIMER_14_BIT;
    ledc_timer.freq_hz          = 50;  
    ledc_timer.clk_cfg          = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    claw_channel_attached = false;

    // Create 1-second timer to turn off the servo after movement completes
    claw_relax_timer = xTimerCreate("claw_relax", pdMS_TO_TICKS(1000), pdFALSE, NULL, claw_relax_cb);

    ESP_LOGI(TAG, "MG90S Claw initialized on GPIO %d (Clamped 0V, No Startup Jitter)", SERVO_PIN);
}

void claw_set_angle(int logical_angle) {
    if (logical_angle < 0) logical_angle = 0;
    if (logical_angle > 138) logical_angle = 138; // Absolute MAX limit restricted to 138
    
    // SAFE MECHANICAL LIMITS: Maps logical 0-138 to physical 60-138
    int physical_angle = 60 + (logical_angle * (138 - 60) / 138);
    
    uint32_t pulse_width = SERVO_MIN_PULSEWIDTH_US + (((SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) * physical_angle) / SERVO_MAX_DEGREE);
    uint32_t duty = (pulse_width * (1 << 14)) / 20000;
    
    attach_claw_channel();
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
    
    claw_current_angle = logical_angle;
    ESP_LOGI(TAG, "Claw set to Logical %d (Physical %d, Duty: %lu)", logical_angle, physical_angle, duty);

    // Reset the auto-relax timer so it turns off 1 second from now
    if (claw_relax_timer != NULL) {
        xTimerReset(claw_relax_timer, 0);
    }
}

void claw_execute_command(const char* cmd) {
    if (strcmp(cmd, "open") == 0) {
        strcpy(claw_last_command_str, "OPEN");
        claw_set_angle(138);
    } else if (strcmp(cmd, "close") == 0) {
        strcpy(claw_last_command_str, "CLOSED");
        claw_set_angle(0);
    } else if (strcmp(cmd, "half_open") == 0) {
        strcpy(claw_last_command_str, "HALF OPEN");
        claw_set_angle(100);
    } else if (strcmp(cmd, "half_close") == 0) {
        strcpy(claw_last_command_str, "HALF CLOSED");
        claw_set_angle(45);
    } else if (strcmp(cmd, "stop") == 0) {
        strcpy(claw_last_command_str, "RELAXED (STOPPED)");
        detach_claw_channel();
    } else {
        ESP_LOGW(TAG, "Unknown claw command: %s", cmd);
    }
}