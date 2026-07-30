// main/claw_controller.cpp
#include "claw_controller.h"
#include "driver/ledc.h"
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

// Auto-Relax Callback: Cuts power to the servo to prevent heating/stalling
static void claw_relax_cb(TimerHandle_t xTimer) {
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
    ESP_LOGI(TAG, "Claw Auto-Relaxed (0 Amps draw) to prevent overheating.");
}

void claw_controller_init() {
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode       = LEDC_LOW_SPEED_MODE;
    ledc_timer.timer_num        = LEDC_TIMER_1;
    ledc_timer.duty_resolution  = LEDC_TIMER_14_BIT;
    ledc_timer.freq_hz          = 50;  
    ledc_timer.clk_cfg          = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {};
    ledc_channel.speed_mode     = LEDC_LOW_SPEED_MODE;
    ledc_channel.channel        = LEDC_CHANNEL_1;
    ledc_channel.timer_sel      = LEDC_TIMER_1;
    ledc_channel.intr_type      = LEDC_INTR_DISABLE;
    ledc_channel.gpio_num       = SERVO_PIN;
    ledc_channel.duty           = 0; 
    ledc_channel.hpoint         = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Create 1-second timer to turn off the servo after movement completes
    claw_relax_timer = xTimerCreate("claw_relax", pdMS_TO_TICKS(1000), pdFALSE, NULL, claw_relax_cb);

    ESP_LOGI(TAG, "MG90S Claw initialized on GPIO %d", SERVO_PIN);
    claw_set_angle(90); 
}

void claw_set_angle(int logical_angle) {
    if (logical_angle < 0) logical_angle = 0;
    if (logical_angle > SERVO_MAX_DEGREE) logical_angle = SERVO_MAX_DEGREE;
    
    // SAFE MECHANICAL LIMITS: Maps logical 0-180 to physical 60-170
    // This prevents the plastic gears from crushing against hard stops and stalling the motor
    int physical_angle = 60 + (logical_angle * 110 / 180);
    
    uint32_t pulse_width = SERVO_MIN_PULSEWIDTH_US + (((SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) * physical_angle) / SERVO_MAX_DEGREE);
    uint32_t duty = (pulse_width * (1 << 14)) / 20000;
    
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
        claw_set_angle(180);
    } else if (strcmp(cmd, "close") == 0) {
        strcpy(claw_last_command_str, "CLOSED");
        claw_set_angle(0);
    } else if (strcmp(cmd, "half_open") == 0) {
        strcpy(claw_last_command_str, "HALF OPEN");
        claw_set_angle(135);
    } else if (strcmp(cmd, "half_close") == 0) {
        strcpy(claw_last_command_str, "HALF CLOSED");
        claw_set_angle(45);
    } else if (strcmp(cmd, "stop") == 0) {
        strcpy(claw_last_command_str, "RELAXED (STOPPED)");
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
    } else {
        ESP_LOGW(TAG, "Unknown claw command: %s", cmd);
    }
}