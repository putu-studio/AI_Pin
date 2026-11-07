// app_wifi_store.c: remember one Wi-Fi network in flash (NVS).
// NVS is a small key-value store: like a dictionary that survives power-off.

#include <stdio.h>
#include "nvs.h"
#include "app_wifi_store.h"

#define STORE_NAMESPACE "wifi"   // our own section of NVS, so keys can't clash
#define KEY_SSID "ssid"
#define KEY_PASS "pass"          // NVS key names must be 15 characters or less

bool app_wifi_store_save(const char *ssid, const char *password)
{
    nvs_handle_t h;
    if (nvs_open(STORE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        printf("WIFI_STORE: can't open\n");
        return false;
    }

    // Write both values, then commit. Nothing is really in flash until commit.
    esp_err_t err = nvs_set_str(h, KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_PASS, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        printf("WIFI_STORE: save failed\n");
        return false;
    }
    printf("WIFI_STORE: saved %s\n", ssid);
    return true;
}

bool app_wifi_store_load(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    nvs_handle_t h;
    // Read-only open fails if we never saved anything. That's fine: nothing saved.
    if (nvs_open(STORE_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    // nvs_get_str needs the buffer size in, and gives the used size back.
    size_t len = ssid_size;
    esp_err_t err = nvs_get_str(h, KEY_SSID, ssid, &len);
    if (err == ESP_OK) {
        len = password_size;
        err = nvs_get_str(h, KEY_PASS, password, &len);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        return false;
    }
    return true;
}

void app_wifi_store_forget(void)
{
    nvs_handle_t h;
    if (nvs_open(STORE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_all(h);   // removes only our "wifi" section, not Wi-Fi's own data
    nvs_commit(h);
    nvs_close(h);
    printf("WIFI_STORE: forgotten\n");
}
