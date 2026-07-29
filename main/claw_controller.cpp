// main/claw_controller.cpp
#include "claw_controller.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CLAW";
char claw_last_command_str[32] = "INITIALIZED";
int claw_current_angle = 90;

// Hardware Pin Definition (ESP32-S3 Zero to MG90S Signal Wire)
#define SERVO_PIN (GPIO_NUM_1) // D0

// Servo Constants (MG90S Standard 180 Degree)
#define SERVO_MIN_PULSEWIDTH_US 500  
#define SERVO_MAX_PULSEWIDTH_US 2500 
#define SERVO_MAX_DEGREE        180  

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

    ESP_LOGI(TAG, "MG90S Claw initialized on GPIO %d", SERVO_PIN);
    claw_set_angle(90); // Default to half open position on boot
}

void claw_set_angle(int logical_angle) {
    if (logical_angle < 0) logical_angle = 0;
    if (logical_angle > SERVO_MAX_DEGREE) logical_angle = SERVO_MAX_DEGREE;
    
    int physical_angle = logical_angle;
    
    uint32_t pulse_width = SERVO_MIN_PULSEWIDTH_US + (((SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) * physical_angle) / SERVO_MAX_DEGREE);
    uint32_t duty = (pulse_width * (1 << 14)) / 20000;
    
    // Explicitly re-bind GPIO 1 to LEDC_CHANNEL_1 to override any robot leg conflicts
    ledc_channel_config_t ledc_channel = {};
    ledc_channel.speed_mode     = LEDC_LOW_SPEED_MODE;
    ledc_channel.channel        = LEDC_CHANNEL_1;
    ledc_channel.timer_sel      = LEDC_TIMER_1;
    ledc_channel.intr_type      = LEDC_INTR_DISABLE;
    ledc_channel.gpio_num       = SERVO_PIN;
    ledc_channel.duty           = duty; 
    ledc_channel.hpoint         = 0;
    ledc_channel_config(&ledc_channel);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
    
    claw_current_angle = logical_angle;
    ESP_LOGI(TAG, "Claw set to Logical %d (Physical %d, Duty: %lu)", logical_angle, physical_angle, duty);
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