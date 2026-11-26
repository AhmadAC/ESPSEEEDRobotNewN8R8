#include "sensor_monitor.h"
#include "servo_controller.h"
#include "audio_player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "SENSOR";

#define ULTRASONIC_TRIG_PIN GPIO_NUM_5 // D4
#define ULTRASONIC_ECHO_PIN GPIO_NUM_6 // D5

// Autonomous safety lock flag
static bool sensor_enabled = false;
static bool safety_lock_engaged = false;
static int32_t distance_threshold = 20;
static int32_t reaction_delay = 0;
static float current_distance = -1.0f;

static TaskHandle_t sensor_task_handle = NULL;
static volatile int64_t echo_rise_us = 0;
static volatile int64_t echo_fall_us = 0;

// Dynamic response buffers
static char tripped_action[16] = "none";
static char cleared_action[16] = "none";
static char tripped_audio[16] = "none";
static char cleared_audio[16] = "none";

static void load_sensor_nvs() {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_i32(my_handle, "sens_thresh", &distance_threshold);
        nvs_get_i32(my_handle, "sens_react", &reaction_delay);

        size_t len = sizeof(tripped_action);
        nvs_get_str(my_handle, "act_trip", tripped_action, &len);

        len = sizeof(cleared_action);
        nvs_get_str(my_handle, "act_clear", cleared_action, &len);

        len = sizeof(tripped_audio);
        nvs_get_str(my_handle, "aud_trip", tripped_audio, &len);

        len = sizeof(cleared_audio);
        nvs_get_str(my_handle, "aud_clear", cleared_audio, &len);

        nvs_close(my_handle);
    }

    if (strlen(tripped_action) == 0) strcpy(tripped_action, "none");
    if (strlen(cleared_action) == 0) strcpy(cleared_action, "none");
    if (strlen(tripped_audio) == 0) strcpy(tripped_audio, "none");
    if (strlen(cleared_audio) == 0) strcpy(cleared_audio, "none");
}

static void ultrasonic_echo_isr(void *arg) {
    int level = gpio_get_level(ULTRASONIC_ECHO_PIN);
    int64_t now = esp_timer_get_time();

    if (level == 1) {
        echo_rise_us = now;
        echo_fall_us = 0;
    } else if (echo_rise_us > 0) {
        echo_fall_us = now;
        if (sensor_task_handle != NULL) {
            BaseType_t hp_woken = pdFALSE;
            vTaskNotifyGiveFromISR(sensor_task_handle, &hp_woken);
            if (hp_woken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

static float read_ultrasonic_distance() {
    echo_rise_us = 0;
    echo_fall_us = 0;
    ulTaskNotifyTake(pdTRUE, 0);

    gpio_set_level(ULTRASONIC_TRIG_PIN, 0);
    esp_rom_delay_us(4);
    gpio_set_level(ULTRASONIC_TRIG_PIN, 1);
    esp_rom_delay_us(12);
    gpio_set_level(ULTRASONIC_TRIG_PIN, 0);

    // Block cleanly on ISR notification without busy-waiting on CPU
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25)) == 0) {
        return -1.0f;
    }

    int64_t rise = echo_rise_us;
    int64_t fall = echo_fall_us;
    if (rise <= 0 || fall <= rise) {
        return -1.0f;
    }

    float distance = (float)(fall - rise) / 58.0f;
    if (distance > 300.0f || distance < 2.0f) {
        return -1.0f;
    }

    return distance;
}

static void ultrasonic_safety_task(void *pvParameter) {
    sensor_task_handle = xTaskGetCurrentTaskHandle();
    bool last_lock_state = false;
    int64_t last_detection_time = 0;
    int consecutive_detections = 0;

    while (1) {
        if (!sensor_enabled) {
            current_distance = -1.0f;
            safety_lock_engaged = false;
            last_lock_state = false;
            consecutive_detections = 0;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        float dist = read_ultrasonic_distance();
        current_distance = dist;

        // Glitch rejection: Require 2 consecutive positive readings to engage safety lock
        if (dist > 0.0f && dist < (float)distance_threshold) {
            consecutive_detections++;
            if (consecutive_detections >= 2) {
                safety_lock_engaged = true;
                last_detection_time = esp_timer_get_time();
            }
        } else {
            consecutive_detections = 0;
            if (safety_lock_engaged && (esp_timer_get_time() - last_detection_time > 1500000)) {
                safety_lock_engaged = false;
            }
        }

        // State Machine Hook
        if (safety_lock_engaged != last_lock_state) {
            if (reaction_delay > 0) {
                vTaskDelay(pdMS_TO_TICKS(reaction_delay));
            }
            if (safety_lock_engaged) {
                ESP_LOGW(TAG, "Lock ENGAGED! Running Action: %s | Audio: %s", tripped_action, tripped_audio);
                if (strcmp(tripped_audio, "none") != 0) audio_play(tripped_audio);
                if (strcmp(tripped_action, "none") != 0) servo_set_action_bypass(tripped_action);
            } else {
                ESP_LOGI(TAG, "Lock RELEASED. Running Action: %s | Audio: %s", cleared_action, cleared_audio);
                if (strcmp(cleared_audio, "none") != 0) audio_play(cleared_audio);
                if (strcmp(cleared_action, "none") != 0) servo_set_action_bypass(cleared_action);
            }
            last_lock_state = safety_lock_engaged;
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void sensor_monitor_init() {
    load_sensor_nvs();
    sensor_enabled = false;

    gpio_config_t trig_io = {};
    trig_io.intr_type = GPIO_INTR_DISABLE;
    trig_io.mode = GPIO_MODE_OUTPUT;
    trig_io.pin_bit_mask = (1ULL << ULTRASONIC_TRIG_PIN);
    trig_io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    trig_io.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&trig_io);
    gpio_set_level(ULTRASONIC_TRIG_PIN, 0);

    gpio_config_t echo_io = {};
    echo_io.intr_type = GPIO_INTR_ANYEDGE;
    echo_io.mode = GPIO_MODE_INPUT;
    echo_io.pin_bit_mask = (1ULL << ULTRASONIC_ECHO_PIN);
    echo_io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    echo_io.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&echo_io);

    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err == ESP_OK || isr_err == ESP_ERR_INVALID_STATE) {
        gpio_isr_handler_add(ULTRASONIC_ECHO_PIN, ultrasonic_echo_isr, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_err));
    }

    xTaskCreatePinnedToCore(ultrasonic_safety_task, "ultrasonic_task", 4096, NULL, 3, &sensor_task_handle, 1);
    ESP_LOGI(TAG, "Ultrasonic Sensor initialized on Trig=GPIO%d, Echo=GPIO%d (Core 1, IRQ-driven)",
             ULTRASONIC_TRIG_PIN, ULTRASONIC_ECHO_PIN);
}

bool sensor_is_enabled() { return sensor_enabled; }

void sensor_set_enabled(bool enabled) {
    sensor_enabled = enabled;
    if (!enabled) {
        safety_lock_engaged = false;
        current_distance = -1.0f;
    }
    ESP_LOGI(TAG, "Ultrasonic safety lock %s", enabled ? "ENABLED" : "DISABLED");
}

int32_t sensor_get_threshold() { return distance_threshold; }

void sensor_set_threshold(int32_t threshold) {
    distance_threshold = threshold;
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_i32(my_handle, "sens_thresh", distance_threshold);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

int32_t sensor_get_reaction_time() { return reaction_delay; }

void sensor_set_reaction_time(int32_t time_ms) {
    reaction_delay = time_ms;
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_i32(my_handle, "sens_react", reaction_delay);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

float sensor_get_distance() { return current_distance; }
bool sensor_is_safety_locked() { return safety_lock_engaged; }

const char* sensor_get_tripped_action() { return tripped_action; }
const char* sensor_get_cleared_action() { return cleared_action; }
const char* sensor_get_tripped_audio() { return tripped_audio; }
const char* sensor_get_cleared_audio() { return cleared_audio; }

void sensor_set_actions(const char* tripped, const char* cleared) {
    strncpy(tripped_action, tripped, sizeof(tripped_action) - 1);
    tripped_action[sizeof(tripped_action) - 1] = '\0';

    strncpy(cleared_action, cleared, sizeof(cleared_action) - 1);
    cleared_action[sizeof(cleared_action) - 1] = '\0';

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "act_trip", tripped_action);
        nvs_set_str(my_handle, "act_clear", cleared_action);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

void sensor_set_audio_actions(const char* tripped, const char* cleared) {
    strncpy(tripped_audio, tripped, sizeof(tripped_audio) - 1);
    tripped_audio[sizeof(tripped_audio) - 1] = '\0';

    strncpy(cleared_audio, cleared, sizeof(cleared_audio) - 1);
    cleared_audio[sizeof(cleared_audio) - 1] = '\0';

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "aud_trip", tripped_audio);
        nvs_set_str(my_handle, "aud_clear", cleared_audio);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}