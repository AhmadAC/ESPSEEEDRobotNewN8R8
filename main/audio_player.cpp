// main\audio_player.cpp
#include "audio_player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "AUDIO";

// Hardware Single-Pin Audio Pin (Waveshare ESP32-S3-Zero Pin D10)
#define AUDIO_PWM_PIN       GPIO_NUM_10 // D10
#define AUDIO_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define AUDIO_LEDC_TIMER    LEDC_TIMER_2 // Timer 2 isolated from Servos (0) and Claw (1)
#define AUDIO_LEDC_CHANNEL  LEDC_CHANNEL_5 // Channel 5 isolated from Servos (0,2,3,4) & Claw (1)

static int32_t current_volume = 80; // 0-100 Default volume
static QueueHandle_t audio_queue;
static volatile bool audio_stop_requested = false;

// Embedded WAV binary data linked by CMakeLists.txt
extern const uint8_t dog_barking_wav_start[] asm("_binary_dog_barking_wav_start");
extern const uint8_t dog_barking_wav_end[]   asm("_binary_dog_barking_wav_end");

static void load_audio_nvs() {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_i32(my_handle, "aud_vol", &current_volume);
        nvs_close(my_handle);
    }
    if (current_volume < 0) current_volume = 0;
    if (current_volume > 100) current_volume = 100;
}

static void audio_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (current_volume == 0 || freq_hz == 0) return;

    ledc_timer_config_t tone_timer = {};
    tone_timer.speed_mode       = AUDIO_LEDC_MODE;
    tone_timer.timer_num        = AUDIO_LEDC_TIMER;
    tone_timer.duty_resolution  = LEDC_TIMER_8_BIT;
    tone_timer.freq_hz          = freq_hz;
    tone_timer.clk_cfg          = LEDC_AUTO_CLK;
    ledc_timer_config(&tone_timer);

    uint32_t duty = (uint32_t)((current_volume * 128) / 100);
    ledc_set_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, duty);
    ledc_update_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_stop(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 0);

    // Restore high-frequency carrier for PCM audio
    tone_timer.freq_hz = 62500;
    ledc_timer_config(&tone_timer);
}

static void audio_task(void *pvParameter) {
    char sound_req[32];
    
    while (1) {
        if (xQueueReceive(audio_queue, &sound_req, portMAX_DELAY)) {
            audio_stop_requested = false;

            const uint8_t* wav_start = NULL;
            const uint8_t* wav_end   = NULL;

            if (strcmp(sound_req, "dog_bark") == 0) {
                wav_start = dog_barking_wav_start;
                wav_end   = dog_barking_wav_end;
            }

            if (wav_start != NULL && wav_end > wav_start) {
                uint32_t sample_rate = 44100;
                uint16_t channels    = 2;
                uint16_t bit_depth   = 16;
                const uint8_t* audio_data = NULL;
                uint32_t data_size = 0;

                // Parse standard WAV header
                if (strncmp((const char*)wav_start, "RIFF", 4) == 0) {
                    size_t offset = 12;
                    while (offset < (size_t)(wav_end - wav_start) - 8) {
                        char chunk_id[5] = {0};
                        memcpy(chunk_id, wav_start + offset, 4);
                        uint32_t chunk_size = *(const uint32_t*)(wav_start + offset + 4);
                        offset += 8;

                        if (strcmp(chunk_id, "fmt ") == 0) {
                            channels    = *(const uint16_t*)(wav_start + offset + 2);
                            sample_rate = *(const uint32_t*)(wav_start + offset + 4);
                            bit_depth   = *(const uint16_t*)(wav_start + offset + 14);
                        } else if (strcmp(chunk_id, "data") == 0) {
                            audio_data = wav_start + offset;
                            data_size  = chunk_size;
                            break;
                        }
                        offset += chunk_size;
                    }
                }

                if (audio_data != NULL && data_size > 0) {
                    if (audio_data + data_size > wav_end) {
                        data_size = (uint32_t)(wav_end - audio_data);
                    }

                    uint32_t us_per_sample = (sample_rate > 0) ? (1000000 / sample_rate) : 22;
                    float vol_mult = (float)current_volume / 100.0f;

                    int64_t next_sample_time = esp_timer_get_time();

                    if (bit_depth == 16) {
                        const int16_t* pcm16 = (const int16_t*)audio_data;
                        size_t total_samples = data_size / 2;
                        size_t step = (channels >= 2) ? channels : 1;

                        for (size_t i = 0; i < total_samples; i += step) {
                            if (audio_stop_requested) break;

                            int32_t sample = (int32_t)(pcm16[i] * vol_mult);
                            // Map signed 16-bit (-32768..32767) to 8-bit unsigned PWM duty (0..255)
                            int32_t duty = (sample >> 8) + 128;
                            if (duty < 0) duty = 0;
                            if (duty > 255) duty = 255;

                            ledc_set_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, (uint32_t)duty);
                            ledc_update_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL);

                            next_sample_time += us_per_sample;
                            int64_t now = esp_timer_get_time();
                            if (next_sample_time > now) {
                                esp_rom_delay_us((uint32_t)(next_sample_time - now));
                            }

                            // Periodically yield to prevent task watchdog triggers
                            if ((i & 0x1FF) == 0) {
                                vTaskDelay(pdMS_TO_TICKS(1));
                                next_sample_time = esp_timer_get_time();
                            }
                        }
                    } else if (bit_depth == 8) {
                        const uint8_t* pcm8 = audio_data;
                        size_t step = (channels >= 2) ? channels : 1;

                        for (size_t i = 0; i < data_size; i += step) {
                            if (audio_stop_requested) break;

                            int32_t sample = (int32_t)pcm8[i] - 128;
                            int32_t duty = (int32_t)(sample * vol_mult) + 128;
                            if (duty < 0) duty = 0;
                            if (duty > 255) duty = 255;

                            ledc_set_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, (uint32_t)duty);
                            ledc_update_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL);

                            next_sample_time += us_per_sample;
                            int64_t now = esp_timer_get_time();
                            if (next_sample_time > now) {
                                esp_rom_delay_us((uint32_t)(next_sample_time - now));
                            }

                            if ((i & 0x1FF) == 0) {
                                vTaskDelay(pdMS_TO_TICKS(1));
                                next_sample_time = esp_timer_get_time();
                            }
                        }
                    }

                    // Turn off pin and discharge coil
                    ledc_stop(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 0);
                    ESP_LOGI(TAG, "Playback on D10 finished.");
                } else {
                    ESP_LOGE(TAG, "Invalid WAV data chunk.");
                }
            } else {
                // If not dog_bark or no file found, provide an audible multi-tone alert on D10
                audio_play_tone(1200, 150);
                vTaskDelay(pdMS_TO_TICKS(50));
                audio_play_tone(1800, 200);
            }

            ledc_stop(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 0);
        }
    }
}

void audio_player_init() {
    load_audio_nvs();
    audio_queue = xQueueCreate(5, sizeof(char) * 32);

    // Initialize 8-bit High-Frequency Carrier (62.5 kHz) on pin D10 (GPIO 10)
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode       = AUDIO_LEDC_MODE;
    ledc_timer.timer_num        = AUDIO_LEDC_TIMER;
    ledc_timer.duty_resolution  = LEDC_TIMER_8_BIT;
    ledc_timer.freq_hz          = 62500; // 62.5 kHz ultrasonic carrier
    ledc_timer.clk_cfg          = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {};
    ledc_channel.speed_mode     = AUDIO_LEDC_MODE;
    ledc_channel.channel        = AUDIO_LEDC_CHANNEL;
    ledc_channel.timer_sel      = AUDIO_LEDC_TIMER;
    ledc_channel.intr_type      = LEDC_INTR_DISABLE;
    ledc_channel.gpio_num       = AUDIO_PWM_PIN;
    ledc_channel.duty           = 0;
    ledc_channel.hpoint         = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Pin D10 stays completely quiet at 0V until audio is played
    ledc_stop(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 0);

    xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "Single-Pin PWM Audio Initialized on D10 (GPIO %d)", AUDIO_PWM_PIN);
}

void audio_set_volume(int volume_pct) {
    if (volume_pct < 0) volume_pct = 0;
    if (volume_pct > 100) volume_pct = 100;
    current_volume = (int32_t)volume_pct;
    
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_i32(my_handle, "aud_vol", current_volume);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

int audio_get_volume() {
    return (int)current_volume;
}

void audio_play(const char* sound_name) {
    if (current_volume == 0) return; 
    
    char req[32];
    strncpy(req, sound_name, sizeof(req) - 1);
    req[sizeof(req) - 1] = '\0';
    
    xQueueSend(audio_queue, &req, 0); 
}

void audio_stop() {
    audio_stop_requested = true; 
    xQueueReset(audio_queue);    
    ledc_stop(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 0);
}