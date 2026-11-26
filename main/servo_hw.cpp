// main\servo_hw.cpp
#include "servo_hw.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "nvs.h"

static const char *TAG = "SERVO_HW";

static int32_t pin_ll = 1;
static int32_t pin_hr = 2;
static int32_t pin_hl = 3;
static int32_t pin_lr = 4;

// Track current angles for smooth interpolation
static int32_t current_angle_ll = 90;
static int32_t current_angle_hr = 90;
static int32_t current_angle_hl = 90;
static int32_t current_angle_lr = 90;

static bool s_servos_active = false;
static bool s_ledc_initialized = false;
static TimerHandle_t s_relax_timer = NULL;
static volatile bool s_relax_requested = false;

static const ledc_channel_t s_channels[4] = {
    SERVO_LOW_LEFT_CH, SERVO_HIGH_RIGHT_CH, SERVO_HIGH_LEFT_CH, SERVO_LOW_RIGHT_CH
};

static void servo_relax_timer_cb(TimerHandle_t xTimer) {
    s_relax_requested = true;
}

static void configure_gpios(void) {
    gpio_config_t cfg = {};
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = (1ULL << pin_ll) | (1ULL << pin_hr) | (1ULL << pin_hl) | (1ULL << pin_lr);
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&cfg);

    const gpio_num_t pins[4] = {(gpio_num_t)pin_ll, (gpio_num_t)pin_hr, (gpio_num_t)pin_hl, (gpio_num_t)pin_lr};
    for (int i = 0; i < 4; i++) {
        gpio_set_level(pins[i], 0);
        rtc_gpio_pullup_dis(pins[i]);
        rtc_gpio_pulldown_en(pins[i]);
    }
}

static void init_ledc_if_needed(void) {
    if (s_ledc_initialized) return;

    ledc_timer_config_t t_cfg = {};
    t_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    t_cfg.duty_resolution = LEDC_TIMER_13_BIT;
    t_cfg.timer_num = LEDC_TIMER_0;
    t_cfg.freq_hz = 50;
    t_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t_cfg));

    const gpio_num_t pins[4] = {(gpio_num_t)pin_ll, (gpio_num_t)pin_hr, (gpio_num_t)pin_hl, (gpio_num_t)pin_lr};
    const uint32_t hpoints[4] = {0, 1500, 3000, 4500};

    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t ch_cfg = {};
        ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        ch_cfg.channel = s_channels[i];
        ch_cfg.timer_sel = LEDC_TIMER_0;
        ch_cfg.intr_type = LEDC_INTR_DISABLE;
        ch_cfg.gpio_num = pins[i];
        ch_cfg.duty = 0;
        ch_cfg.hpoint = hpoints[i];
        ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    }
    s_ledc_initialized = true;
    ESP_LOGI(TAG, "LEDC Initialized (Pins: LL=%ld, HR=%ld, HL=%ld, LR=%ld)",
             (long)pin_ll, (long)pin_hr, (long)pin_hl, (long)pin_lr);
}

void servo_hw_init(void) {
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "pin_ll", &pin_ll);
        nvs_get_i32(h, "pin_hr", &pin_hr);
        nvs_get_i32(h, "pin_hl", &pin_hl);
        nvs_get_i32(h, "pin_lr", &pin_lr);
        nvs_close(h);
    }
    if (pin_ll <= 0) pin_ll = 1;
    if (pin_hr <= 0) pin_hr = 2;
    if (pin_hl <= 0) pin_hl = 3;
    if (pin_lr <= 0) pin_lr = 4;

    configure_gpios();
    s_servos_active = false;
    s_ledc_initialized = false;
    s_relax_requested = false;
    
    // Reset current angles
    current_angle_ll = 90;
    current_angle_hr = 90;
    current_angle_hl = 90;
    current_angle_lr = 90;

    s_relax_timer = xTimerCreate("srv_rlx", pdMS_TO_TICKS(4000), pdFALSE, NULL, servo_relax_timer_cb);
}

void servo_hw_set_pins(int ll, int hr, int hl, int lr) {
    if (ll <= 0 || hr <= 0 || hl <= 0 || lr <= 0) return;
    servo_hw_relax_all();
    if (s_ledc_initialized) {
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        s_ledc_initialized = false;
    }
    pin_ll = ll; pin_hr = hr; pin_hl = hl; pin_lr = lr;

    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "pin_ll", pin_ll);
        nvs_set_i32(h, "pin_hr", pin_hr);
        nvs_set_i32(h, "pin_hl", pin_hl);
        nvs_set_i32(h, "pin_lr", pin_lr);
        nvs_commit(h);
        nvs_close(h);
    }
    configure_gpios();
}

void servo_hw_get_pins(int *ll, int *hr, int *hl, int *lr) {
    if (ll) *ll = (int)pin_ll;
    if (hr) *hr = (int)pin_hr;
    if (hl) *hl = (int)pin_hl;
    if (lr) *lr = (int)pin_lr;
}

// NEW: Smooth move function with interpolation
void servo_hw_smooth_write(ledc_channel_t channel, int32_t target_angle, int32_t offset, 
                           int32_t *current_angle, int step_size, int delay_ms) {
    if (!s_servos_active) return;
    init_ledc_if_needed();

    int32_t final_target = target_angle + offset;
    if (channel == SERVO_LOW_RIGHT_CH) {
        if (final_target < 4) final_target = 4;
    } else {
        if (final_target < 0) final_target = 0;
    }
    if (final_target > 180) final_target = 180;

    int32_t current = *current_angle + offset;
    int direction = (final_target > current) ? 1 : -1;
    
    while (current != final_target) {
        current += direction * step_size;
        
        // Clamp to target
        if ((direction > 0 && current > final_target) || (direction < 0 && current < final_target)) {
            current = final_target;
        }
        
        uint32_t duty = 205 + ((1024 - 205) * current) / 180;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
        
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    *current_angle = target_angle;
}

void servo_hw_write(ledc_channel_t channel, int32_t target_angle, int32_t offset) {
    if (!s_servos_active) return;
    init_ledc_if_needed();

    int32_t final_angle = target_angle + offset;
    if (channel == SERVO_LOW_RIGHT_CH) {
        if (final_angle < 4) final_angle = 4;
    } else {
        if (final_angle < 0) final_angle = 0;
    }
    if (final_angle > 180) final_angle = 180;

    uint32_t duty = 205 + ((1024 - 205) * final_angle) / 180;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    
    // Update current angle tracking
    if (channel == SERVO_LOW_LEFT_CH) current_angle_ll = target_angle;
    else if (channel == SERVO_HIGH_RIGHT_CH) current_angle_hr = target_angle;
    else if (channel == SERVO_HIGH_LEFT_CH) current_angle_hl = target_angle;
    else if (channel == SERVO_LOW_RIGHT_CH) current_angle_lr = target_angle;
}

void servo_hw_relax_all(void) {
    s_servos_active = false;
    servo_hw_cancel_relax();

    if (s_ledc_initialized) {
        for (int i = 0; i < 4; i++) {
            ledc_stop(LEDC_LOW_SPEED_MODE, s_channels[i], 0);
        }
    }
    const gpio_num_t pins[4] = {(gpio_num_t)pin_ll, (gpio_num_t)pin_hr, (gpio_num_t)pin_hl, (gpio_num_t)pin_lr};
    for (int i = 0; i < 4; i++) {
        gpio_set_level(pins[i], 0);
    }
}

void servo_hw_schedule_relax(void) {
    s_relax_requested = false;
    if (s_relax_timer != NULL) {
        xTimerReset(s_relax_timer, 0);
    }
}

void servo_hw_cancel_relax(void) {
    s_relax_requested = false;
    if (s_relax_timer != NULL) {
        xTimerStop(s_relax_timer, 0);
    }
}

bool servo_hw_check_and_clear_relax_req(void) {
    if (s_relax_requested) {
        s_relax_requested = false;
        return true;
    }
    return false;
}

bool servo_hw_is_active(void) { return s_servos_active; }
void servo_hw_set_active(bool active) { s_servos_active = active; }
