

#include "sensor_monitor.h"
#include "servo_controller.h"
#include "audio_player.h"
#include "ble_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SENSOR";

static int32_t current_trig_pin = 5;
static int32_t current_echo_pin = 6;

// Disabled automatically by default on startup
static bool sensor_safety_enabled = false;
static bool safety_lock_engaged = false;
static int32_t distance_threshold = 20;
static int32_t reaction_delay = 0;
static float current_distance = -1.0f;

static TaskHandle_t sensor_task_handle = NULL;

static char tripped_action[16] = "sit";
static char cleared_action[16] = "stand";
static char tripped_audio[16] = "beep";
static char cleared_audio[16] = "none";

static void load_sensor_nvs() {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_i32(my_handle, "sens_trig", &current_trig_pin);
        nvs_get_i32(my_handle, "sens_echo", &current_echo_pin);
        nvs_get_i32(my_handle, "sens_thresh", &distance_threshold);
        nvs_get_i32(my_handle, "sens_react", &reaction_delay);

        uint8_t en = 0; // Default: disabled
        if (nvs_get_u8(my_handle, "sens_en", &en) == ESP_OK) {
            sensor_safety_enabled = (en != 0);
        } else {
            sensor_safety_enabled = false;
        }

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

    if (current_trig_pin <= 0) current_trig_pin = 5;
    if (current_echo_pin <= 0) current_echo_pin = 6;
    if (strlen(tripped_action) == 0) strcpy(tripped_action, "sit");
    if (strlen(cleared_action) == 0) strcpy(cleared_action, "stand");
    if (strlen(tripped_audio) == 0) strcpy(tripped_audio, "beep");
    if (strlen(cleared_audio) == 0) strcpy(cleared_audio, "none");
}

static void configure_sensor_gpios() {
    gpio_reset_pin((gpio_num_t)current_trig_pin);
    gpio_reset_pin((gpio_num_t)current_echo_pin);

    gpio_config_t trig_io = {};
    trig_io.intr_type = GPIO_INTR_DISABLE;
    trig_io.mode = GPIO_MODE_OUTPUT;
    trig_io.pin_bit_mask = (1ULL << current_trig_pin);
    trig_io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    trig_io.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&trig_io);
    gpio_set_level((gpio_num_t)current_trig_pin, 0);

    gpio_config_t echo_io = {};
    echo_io.intr_type = GPIO_INTR_DISABLE;
    echo_io.mode = GPIO_MODE_INPUT;
    echo_io.pin_bit_mask = (1ULL << current_echo_pin);
    echo_io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    echo_io.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&echo_io);
}

static float read_ultrasonic_distance() {
    // If Echo line is stuck HIGH, release it
    if (gpio_get_level((gpio_num_t)current_echo_pin) != 0) {
        gpio_set_direction((gpio_num_t)current_echo_pin, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)current_echo_pin, 0);
        esp_rom_delay_us(100);
        gpio_set_direction((gpio_num_t)current_echo_pin, GPIO_MODE_INPUT);
        esp_rom_delay_us(10);
    }

    gpio_set_level((gpio_num_t)current_trig_pin, 0);
    esp_rom_delay_us(4);

    // 15us HIGH trigger pulse
    gpio_set_level((gpio_num_t)current_trig_pin, 1);
    esp_rom_delay_us(15);
    gpio_set_level((gpio_num_t)current_trig_pin, 0);

    int64_t wait_start = esp_timer_get_time();
    while (gpio_get_level((gpio_num_t)current_echo_pin) == 0) {
        if (esp_timer_get_time() - wait_start > 25000) {
            return -1.0f;
        }
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level((gpio_num_t)current_echo_pin) == 1) {
        if (esp_timer_get_time() - echo_start > 30000) {
            return -1.0f;
        }
    }
    int64_t echo_end = esp_timer_get_time();
    int64_t duration = echo_end - echo_start;

    if (duration < 60 || duration > 30000) {
        return -1.0f;
    }

    float distance = (float)duration / 58.0f;
    if (distance > 400.0f || distance < 2.0f) {
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
        if (!sensor_safety_enabled) {
            current_distance = -1.0f;
            safety_lock_engaged = false;
            last_lock_state = false;
            consecutive_detections = 0;
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        float dist = read_ultrasonic_distance();
        current_distance = dist;

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

        if (safety_lock_engaged != last_lock_state) {
            if (reaction_delay > 0) {
                vTaskDelay(pdMS_TO_TICKS(reaction_delay));
            }
            if (safety_lock_engaged) {
                ESP_LOGW(TAG, "Obstacle trip (%.1fcm < %ldcm)! Action: %s | Audio: %s",
                         dist, (long)distance_threshold, tripped_action, tripped_audio);
                if (strcmp(tripped_audio, "none") != 0) audio_play(tripped_audio);
                if (strcmp(tripped_action, "none") != 0) servo_set_action_bypass(tripped_action);
            } else {
                ESP_LOGI(TAG, "Obstacle cleared. Action: %s | Audio: %s", cleared_action, cleared_audio);
                if (strcmp(cleared_audio, "none") != 0) audio_play(cleared_audio);
                if (strcmp(cleared_action, "none") != 0) servo_set_action_bypass(cleared_action);
            }
            last_lock_state = safety_lock_engaged;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void sensor_monitor_init() {
    load_sensor_nvs();
    configure_sensor_gpios();

    xTaskCreatePinnedToCore(ultrasonic_safety_task, "ultrasonic_task", 3072, NULL, 3, &sensor_task_handle, 1);
    ESP_LOGI(TAG, "Ultrasonic HC-SR04 initialized (Trig=GPIO%ld, Echo=GPIO%ld, Safety=DISABLED)",
             (long)current_trig_pin, (long)current_echo_pin);
}

void sensor_set_pins(int trig, int echo) {
    if (trig <= 0 || echo <= 0 || trig == echo) return;
    current_trig_pin = trig;
    current_echo_pin = echo;

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_i32(my_handle, "sens_trig", current_trig_pin);
        nvs_set_i32(my_handle, "sens_echo", current_echo_pin);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    configure_sensor_gpios();
    ESP_LOGI(TAG, "Ultrasonic Sensor pins updated: Trig=GPIO%ld, Echo=GPIO%ld",
             (long)current_trig_pin, (long)current_echo_pin);
}

void sensor_get_pins(int *trig, int *echo) {
    if (trig) *trig = (int)current_trig_pin;
    if (echo) *echo = (int)current_echo_pin;
}

bool sensor_is_enabled() { return sensor_safety_enabled; }

void sensor_set_enabled(bool enabled) {
    sensor_safety_enabled = enabled;
    if (!enabled) {
        safety_lock_engaged = false;
        current_distance = -1.0f;
    }

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, "sens_en", enabled ? 1 : 0);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }

    ESP_LOGI(TAG, "Ultrasonic automatic safety intervention %s", enabled ? "ARMED" : "DISARMED");
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