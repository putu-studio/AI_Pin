#include "driver/i2s_pdm.h"
#include "app_mic.h"
#include "board.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include "esp_heap_caps.h"
#include <string.h>
#include "app_out.h"

#define MAX_REC_SECONDS 120
#define REC_BYTES (MAX_REC_SECONDS * 16000 * 2)

static i2s_chan_handle_t rx_chan;
static int16_t *rec_buf;          // MAX_REC_SECONDS of audio
static size_t rec_len;            // bytes recorded so far

static volatile bool recording = false;   // main.c sets this, mic_task reads it
static volatile bool full = false;        // mic_task sets this, main.c reads it
static TaskHandle_t mic_task_handle;      // lets app_mic_start_recording wake mic_task


static void mic_task(void *args){
    static int16_t buf[512];
    size_t bytes_read;

    while(1){
        // Sleep here, using no CPU, until app_mic_start_recording wakes us.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Turn the mic's clock on. With no clock, the mic itself goes to sleep too.
        ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
        printf("MIC: ON\n");

        while (recording) {
            i2s_channel_read(rx_chan, buf, sizeof(buf), &bytes_read, portMAX_DELAY);

            if (rec_len + bytes_read <= REC_BYTES) {
                // Copy this chunk to the end of what we have so far.
                memcpy((uint8_t *)rec_buf + rec_len, buf, bytes_read);
                rec_len = rec_len + bytes_read;
            } else {
                // No room left. main.c sees this and sends the recording.
                full = true;
            }
        }

        ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
        printf("MIC: OFF\n");
    }
}

esp_err_t app_mic_init(void){
    
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));
    
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = MIC_CLK,
            .din = MIC_DATA,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_rx_cfg));
    
    rec_buf = heap_caps_malloc(REC_BYTES, MALLOC_CAP_SPIRAM);

    if (rec_buf != NULL){
        printf("Mic Ready\n");
        xTaskCreate(mic_task, "mic", 4096, NULL, 5, &mic_task_handle); // Stack size 4096 bytes, priority 5
    } else {
        printf("Mic not ready as PSRAM allocation failed\n");
        return ESP_FAIL;
    }

    return ESP_OK;
}


void app_mic_start_recording(void){
    // Order matters: reset everything before mic_task starts copying.
    rec_len = 0;
    full = false;
    recording = true;
    xTaskNotifyGive(mic_task_handle);   // wake mic_task so it turns the mic on
}

void app_mic_stop_recording(void){
    recording = false;
}

bool app_mic_is_full(void){
    return full;
}

const int16_t *app_mic_get_audio(void){
    return rec_buf;
}

size_t app_mic_get_length(void){
    return rec_len;
}






