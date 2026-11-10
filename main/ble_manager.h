// 11-3ESPSEEEDRobotNewN8R8/main/ble_manager.h
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ble_manager_init(void);
void ble_manager_notify_ip(const char* ip);
bool ble_manager_is_connected(void);
void ble_manager_send_status(const char* status_json);
void ble_manager_send_notify(const char* message);

#ifdef __cplusplus
}
#endif