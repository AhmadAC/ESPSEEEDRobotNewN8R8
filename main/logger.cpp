// main\logger.cpp
#include "logger.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "LOGGER";
static const char *LOG_PATH = "/spiffs/debug.log";
static const char *LOG_PATH_OLD = "/spiffs/debug.log.old";
static const size_t MAX_LOG_SIZE = 512 * 1024; // 512 KB per file

static SemaphoreHandle_t log_mutex = NULL;
static vprintf_like_t original_vprintf = NULL;
static bool spiffs_mounted = false;

static int file_vprintf(const char *fmt, va_list args) {
    // 1. Always mirror to console (USB Serial/JTAG or UART)
    va_list args_copy;
    va_copy(args_copy, args);
    int ret = 0;
    if (original_vprintf) {
        ret = original_vprintf(fmt, args_copy);
    } else {
        ret = vprintf(fmt, args_copy);
    }
    va_end(args_copy);

    // 2. Prevent writing to flash from interrupt service routines or timer service tasks
    if (xPortInIsrContext() || !spiffs_mounted || log_mutex == NULL) {
        return ret;
    }

    // Timer tasks (Tmr Svc / esp_timer) have very small stacks; skip flash I/O inside them
    const char *task_name = pcTaskGetName(NULL);
    if (task_name && (strcmp(task_name, "Tmr Svc") == 0 || strcmp(task_name, "esp_timer") == 0)) {
        return ret;
    }

    // 3. Acquire mutex with a short timeout to prevent blocking real-time tasks
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
        FILE *f = fopen(LOG_PATH, "a");
        if (f) {
            vfprintf(f, fmt, args);
            fflush(f);

            long cur_pos = ftell(f);
            fclose(f);

            // Log rotation: if file exceeds MAX_LOG_SIZE, rename to .old and start clean
            if (cur_pos > (long)MAX_LOG_SIZE) {
                unlink(LOG_PATH_OLD);
                rename(LOG_PATH, LOG_PATH_OLD);
            }
        }
        xSemaphoreGive(log_mutex);
    }

    return ret;
}

esp_err_t logger_init(void) {
    log_mutex = xSemaphoreCreateMutex();

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("spiffs", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS Partition Mounted: %d KB total, %d KB used", (int)(total / 1024), (int)(used / 1024));
    }
    spiffs_mounted = true;

    // Log boot banner to file
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        FILE *f = fopen(LOG_PATH, "a");
        if (f) {
            fprintf(f, "\n\n==================== [SYSTEM BOOT] ====================\n");
            fclose(f);
        }
        xSemaphoreGive(log_mutex);
    }

    // Hook the ESP-IDF logging system
    original_vprintf = esp_log_set_vprintf(file_vprintf);
    ESP_LOGI(TAG, "Persistent file logging active (/spiffs/debug.log)");

    return ESP_OK;
}

void logger_print_all(void) {
    if (!spiffs_mounted || log_mutex == NULL) {
        printf("[LOGGER] Flash logging is not initialized.\n");
        return;
    }

    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FILE *f = fopen(LOG_PATH, "r");
        if (!f) {
            printf("[LOGGER] No log file exists yet.\n");
            xSemaphoreGive(log_mutex);
            return;
        }

        printf("\n==================== [DEBUG LOG DUMP START] ====================\n");
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            fputs(line, stdout);
        }
        printf("\n==================== [DEBUG LOG DUMP END] ======================\n");
        fclose(f);
        xSemaphoreGive(log_mutex);
    }
}

void logger_print_tail(int lines) {
    if (!spiffs_mounted || log_mutex == NULL) {
        printf("[LOGGER] Flash logging is not initialized.\n");
        return;
    }

    if (lines <= 0) lines = 50;

    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FILE *f = fopen(LOG_PATH, "r");
        if (!f) {
            printf("[LOGGER] No log file exists yet.\n");
            xSemaphoreGive(log_mutex);
            return;
        }

        // Count total lines
        int total_lines = 0;
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            total_lines++;
        }

        int skip_lines = (total_lines > lines) ? (total_lines - lines) : 0;
        rewind(f);

        int current = 0;
        printf("\n==================== [DEBUG LOG TAIL (LAST %d LINES)] ====================\n", lines);
        while (fgets(buf, sizeof(buf), f)) {
            if (current >= skip_lines) {
                fputs(buf, stdout);
            }
            current++;
        }
        printf("==================== [END OF TAIL] ====================\n");

        fclose(f);
        xSemaphoreGive(log_mutex);
    }
}

void logger_clear(void) {
    if (!spiffs_mounted || log_mutex == NULL) return;

    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        unlink(LOG_PATH);
        unlink(LOG_PATH_OLD);
        FILE *f = fopen(LOG_PATH, "w");
        if (f) {
            fprintf(f, "[SYSTEM] Log cleared.\n");
            fclose(f);
        }
        xSemaphoreGive(log_mutex);
        printf("[LOGGER] Persistent log file cleared successfully.\n");
    }
}

size_t logger_get_size(void) {
    struct stat st;
    if (stat(LOG_PATH, &st) == 0) {
        return (size_t)st.st_size;
    }
    return 0;
}

esp_err_t logger_http_stream(void *http_req) {
    httpd_req_t *req = (httpd_req_t *)http_req;
    httpd_resp_set_type(req, "text/plain");

    if (!spiffs_mounted || log_mutex == NULL) {
        httpd_resp_sendstr(req, "SPIFFS Logging not mounted.");
        return ESP_OK;
    }

    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FILE *f = fopen(LOG_PATH, "r");
        if (!f) {
            httpd_resp_sendstr(req, "Log file is currently empty.");
            xSemaphoreGive(log_mutex);
            return ESP_OK;
        }

        char chunk[1024];
        size_t bytes_read = 0;
        while ((bytes_read = fread(chunk, 1, sizeof(chunk), f)) > 0) {
            httpd_resp_send_chunk(req, chunk, bytes_read);
        }
        httpd_resp_send_chunk(req, NULL, 0); // End chunking
        fclose(f);
        xSemaphoreGive(log_mutex);
    }

    return ESP_OK;
}