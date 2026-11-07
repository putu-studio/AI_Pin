#include "board.h"
#include "esp_sleep.h"

void board_button_init(void){
    ESP_ERROR_CHECK(gpio_reset_pin(BTN_GND_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(BTN_GND_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(BTN_GND_GPIO, 0));


    ESP_ERROR_CHECK(gpio_reset_pin(BTN_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(BTN_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_pullup_en(BTN_GPIO));
}

bool board_button_pressed(void){
    return gpio_get_level(BTN_GPIO) == 0;
}

void board_sleep_until_button(int timeout_ms){
    // The button pulls the pin low, so a low level is what wakes the chip.
    ESP_ERROR_CHECK(gpio_wakeup_enable(BTN_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    // Forget any alarm from last time, then set a new one if we were asked to.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (timeout_ms > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)timeout_ms * 1000);   // this one takes microseconds
    }

    // The CPU stops on this line. Memory and pin levels are kept, so when it
    // wakes it just carries on from the next line.
    esp_light_sleep_start();

    gpio_wakeup_disable(BTN_GPIO);
}