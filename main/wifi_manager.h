#pragma once
#include <stdint.h>

void wifi_manager_init();
void wifi_manager_connect_async(const char* ssid, const char* pass);
char* wifi_scan_networks_json();
void wifi_save_credentials(const char* ssid, const char* pass);
void wifi_manager_force_ap_temporary();
void wifi_set_tx_power(int8_t power);