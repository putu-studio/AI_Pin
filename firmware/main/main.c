#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "app_console.h"
#include "app_wifi.h"
#include "esp_log.h"
#include "app_time.h"
#include "app_display.h"
#include "board.h"
#include "app_out.h"
#include "app_relay.h"
#include "app_mic.h"
#include "app_ui.h"
#include "app_wifi_store.h"


// Ask on serial: scan, pick a network, type the password, connect.
// Saves the network if it works, so next boot skips all of this.
static bool wifi_from_menu(void)
{
    int count = app_wifi_scan_and_list();
    if (count <= 0) {
        out_printf("No networks found \n");
        return false;
    }

    int choice = app_console_read_number("Pick a network", 1, count);
    const wifi_ap_record_t *ap = app_wifi_get_ap(choice - 1);
    out_printf("Selected: %s\n", (char *)ap->ssid);

    char password[65] = "";
    if (ap->authmode != WIFI_AUTH_OPEN) {
        out_printf("Type password: ");
        fflush(stdout);
        app_console_read_password(password, sizeof(password));
    }

    if (!app_wifi_connect(ap, password)) {
        return false;
    }
    app_wifi_store_save((const char *)ap->ssid, password);   // remember it for next boot
    return true;
}

// Use the saved network if there is one, otherwise ask on serial.
// Hold the button while it boots to forget the saved network.
static bool connect_wifi(void)
{
    if (board_button_pressed()) {
        out_printf("Button held: forgetting saved Wi-Fi\n");
        app_wifi_store_forget();
    }

    char ssid[33] = "";       // Wi-Fi names are at most 32 characters, + '\0'
    char password[65] = "";   // passwords are at most 64 characters, + '\0'

    if (app_wifi_store_load(ssid, sizeof(ssid), password, sizeof(password))) {
        out_printf("Saved Wi-Fi: %s\n", ssid);
        if (app_wifi_connect_ssid(ssid, password)) {
            return true;
        }
        out_printf("Saved Wi-Fi failed. Pick one:\n");
    }

    // Nothing saved, or it didn't work: keep asking until one connects.
    while (!wifi_from_menu()) {
        out_printf("Wifi failed to connect. Try again.\n");
    }
    return true;
}



void app_main(void)
{
    board_button_init();
    
    esp_log_level_set("wifi", ESP_LOG_WARN);

    ESP_ERROR_CHECK(app_display_init());
    app_mic_init();
    
    app_console_init();
    app_wifi_start();
    if (!connect_wifi()) {
        out_printf("Wifi failed to connect\n");
        return;
    }

    if (!app_time_sync()) {
        out_printf ("Could not sync time\n");
        return;
    } 

    app_ui_run();   // never returns: the device runs from here
}
