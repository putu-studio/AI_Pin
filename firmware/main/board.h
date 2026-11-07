#pragma once
#include "driver/gpio.h"
#include <stdbool.h>
#include "sdkconfig.h"


#if CONFIG_IDF_TARGET_ESP32S3

    #define LED_GPIO GPIO_NUM_21

    #define LCD_SCK GPIO_NUM_7
    #define LCD_MOSI GPIO_NUM_9
    #define LCD_CS GPIO_NUM_4
    #define LCD_DC GPIO_NUM_5
    #define LCD_RST GPIO_NUM_6
    #define LCD_BL GPIO_NUM_43

    #define MIC_DATA GPIO_NUM_41
    #define MIC_CLK GPIO_NUM_42

#else
    #error "This firmware targets the ESP32-S3 (Seeed XIAO ESP32-S3 Sense). Run: idf.py set-target esp32s3"
#endif

#define BTN_GPIO        GPIO_NUM_1
#define BTN_GND_GPIO    GPIO_NUM_2


#define LED_ON 0 //Active low
#define LED_OFF 1 //Active low

#define BL_ON 1 // Display backlight
#define BL_OFF 0
#define DISPLAY_DARK_THEME 1 // 1 = white on black (TFT), 0 = black on white (e-ink)


void board_button_init(void);
bool board_button_pressed(void);

// Light sleep until the button is pressed, or until timeout_ms has passed.
// A timeout of 0 means sleep until the button, however long that takes.
void board_sleep_until_button(int timeout_ms);