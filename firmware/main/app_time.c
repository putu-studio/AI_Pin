#include <stdio.h>
#include "freertos/FreeRTOS.h"   // pdMS_TO_TICKS
#include "esp_err.h"             // ESP_ERROR_CHECK
#include "esp_netif_sntp.h"
#include "app_time.h"
#include "app_out.h"

static bool s_sntp_started = false;

bool app_time_sync(void) {
    
    if (!s_sntp_started){
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));
        s_sntp_started = true;
    }

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK) {
            out_printf("Time synced\n");
            return true;
    } else {
            out_printf("Time sync failed\n");
            return false;
        }
}
