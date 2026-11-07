#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


esp_err_t app_mic_init(void);


void app_mic_start_recording(void);              // clears old audio, starts recording
void app_mic_stop_recording(void);               // stops recording
bool app_mic_is_full(void);                      // true when the buffer has no room left
const int16_t *app_mic_get_audio(void);          // the recorded samples
size_t app_mic_get_length(void);                 // how many bytes were recorded