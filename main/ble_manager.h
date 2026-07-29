#pragma once
#include <stdbool.h>

void ble_manager_init();
void ble_manager_notify_ip(const char* ip);
bool ble_manager_is_connected();
void ble_manager_send_status(const char* status_json);