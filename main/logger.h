// main\logger.h
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize SPIFFS and register the system vprintf logger
esp_err_t logger_init(void);

// Print the entire log file out to stdout (USB console)
void logger_print_all(void);

// Print the last N lines out to stdout
void logger_print_tail(int lines);

// Clear the log file
void logger_clear(void);

// Get current log size in bytes
size_t logger_get_size(void);

// Stream the log file chunk-by-chunk to an HTTP response
esp_err_t logger_http_stream(void *http_req);

#ifdef __cplusplus
}
#endif