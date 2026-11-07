#pragma once
#include "esp_err.h"

esp_err_t app_display_init(void);
void app_display_text(const char *text);
void app_display_append(const char *text);

void app_display_backspace(void);
// Orange question, text with an orange dot beside some lines, and a "2/3" counter.
void app_display_page(const char *question, const char *text, const int *dot_lines, int dot_count, int page_number, int total_pages);

// What moves above the title on a status screen.
typedef enum {
    DISPLAY_STILL_DOT,     // a calm orange dot: waiting for you
    DISPLAY_PULSING_DOT,   // the dot fades in and out: listening to you
    DISPLAY_SPINNER,       // a spinning ring: busy working
} display_indicator_t;

void app_display_status(const char *title, const char *hint, display_indicator_t indicator);

// Turn the whole screen off to save power, and back on with the same picture.
void app_display_screen_off(void);
void app_display_screen_on(void);
