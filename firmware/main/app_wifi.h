#pragma once

#include <stdbool.h>
#include "esp_wifi.h"

void app_wifi_start(void);

// Power saving: the radio is only on while a question is being asked.
void app_wifi_turn_on(void);                     // starts connecting, doesn't wait
bool app_wifi_wait_connected(int timeout_ms);    // true once it's connected
void app_wifi_turn_off(void);

int app_wifi_scan_and_list(void);

const wifi_ap_record_t *app_wifi_get_ap(int index);

// Connects to `ap` with `password` ("" for open networks).
// Waits up to 20 seconds. Returns true if connected.
bool app_wifi_connect(const wifi_ap_record_t *ap, const char *password);

// Same, but by network name. Used to reconnect to the saved network.
bool app_wifi_connect_ssid(const char *ssid, const char *password);
