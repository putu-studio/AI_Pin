#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "app_wifi.h"
#include "app_out.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"


static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRIES         3
static int s_wifi_retries = 0;
static bool s_wifi_is_on = false;

// 60 means 15 dBm (the unit is 0.25 dBm). The default is 20 dBm. Lower power means
// smaller current spikes when the radio sends, at the cost of some range.
#define WIFI_TX_POWER 60


#define MAX_APS 12
static wifi_ap_record_t list_aps[MAX_APS];
static int ap_count = 0;


static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data){

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED){
        wifi_event_sta_disconnected_t *d = data;
        printf("Wi-Fi disconnected, reason %d\n", d->reason);
        
        if (s_wifi_retries < MAX_RETRIES){
            printf("Retrying\n");
            esp_wifi_connect(); 
            s_wifi_retries++;
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t *ip = data;
        printf("Wifi Connected. IP is "IPSTR"\n", IP2STR(&ip->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        s_wifi_retries = 0;
    }

}

void app_wifi_start(void){
    
    // Wi-Fi needs NVS (flash) to store its calibration data
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND){
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Netif setup: for IP address (DHCP) so that it can talk to internet, and TCP/IP.
    // Wifi runs in the background and reports app about events
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Wifi radio on
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // Station means a device. The router is the Access point. 

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(WIFI_TX_POWER));
    s_wifi_is_on = true;

}

// Turn the radio on and start connecting to the last network, without waiting.
// Doing nothing if it's already on means it's safe to call this twice.
void app_wifi_turn_on(void){
    if (s_wifi_is_on) {
        return;
    }
    s_wifi_is_on = true;
    printf("WIFI: ON\n");

    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_wifi_retries = 0;
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(WIFI_TX_POWER));   // has to be set again after every start
    ESP_ERROR_CHECK(esp_wifi_connect());
}

// Wait for the connecting that app_wifi_turn_on started. True if it worked.
bool app_wifi_wait_connected(int timeout_ms){
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

// Turn the radio off completely. It keeps the network details for next time.
void app_wifi_turn_off(void){
    if (!s_wifi_is_on) {
        return;
    }
    s_wifi_is_on = false;
    printf("WIFI: OFF\n");

    s_wifi_retries = MAX_RETRIES;   // this disconnect is on purpose, so don't reconnect
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_stop());
}

int app_wifi_scan_and_list(void){
    out_printf("Scanning for Wi-fi:\n");

    wifi_scan_config_t scan_cfg = {
        .scan_time.active.min = 200,
        .scan_time.active.max = 400, 
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, true)); // Null for default. Blocking when set to true.
    uint16_t count = MAX_APS;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, list_aps)); // count as input is max APs can be held, as output is the actual number of APs
    ap_count = count;

    for (int i = 0; i < ap_count; i++){
        out_printf("%d. %s %d dBm\n", i+1, (char *)list_aps[i].ssid, list_aps[i].rssi);
    }
    return ap_count;
}

const wifi_ap_record_t *app_wifi_get_ap(int index){
    if (index < 0 || index >= ap_count){
        return NULL;
    }
    return &list_aps[index];
}

// Connect to a network by its name. Used for both the menu and the saved network.
bool app_wifi_connect_ssid(const char *ssid, const char *password){
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    // Clear old results, so a failure from an earlier try doesn't count now.
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_wifi_retries = 0;
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t conn_bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE , pdFALSE, pdMS_TO_TICKS(20000));

    if (conn_bits & WIFI_CONNECTED_BIT){
        out_printf("Wifi Connected\n");
        return true;
    }

    // Failed: stop trying this network, so we can try another one.
    // Setting retries to the max first stops the event handler from reconnecting.
    s_wifi_retries = MAX_RETRIES;
    esp_wifi_disconnect();
    return false;
}

bool app_wifi_connect(const wifi_ap_record_t *ap, const char *password){
    return app_wifi_connect_ssid((const char *)ap->ssid, password);
}
