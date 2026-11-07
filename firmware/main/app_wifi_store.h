#pragma once
#include <stdbool.h>
#include <stddef.h>

// Saves the Wi-Fi name and password in flash (NVS), so they survive power-off.
// NVS must already be started (app_wifi_start does that).

// Save after a successful connect. Returns true if saved.
bool app_wifi_store_save(const char *ssid, const char *password);

// Read the saved network. Returns false if nothing is saved yet.
bool app_wifi_store_load(char *ssid, size_t ssid_size, char *password, size_t password_size);

// Forget the saved network.
void app_wifi_store_forget(void);
