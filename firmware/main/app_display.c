#include <stdio.h>
#include "app_display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_commands.h"   // LCD_CMD_SWRESET
#include "driver/spi_master.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "esp_timer.h"
#include <sys/param.h>
#include <unistd.h>
#include <sys/lock.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#if DISPLAY_DARK_THEME
#define THEME_FG lv_color_hex(0xF5F4EF)      // warm off-white text
#define THEME_BG lv_color_hex(0x1F1E1D)      // warm near-black background
#define THEME_HINT lv_color_hex(0x9B9A94)    // gray for small hint text
#define THEME_TRACK lv_color_hex(0x3A3936)   // dark gray ring behind the spinner
#else
#define THEME_FG lv_color_black()
#define THEME_BG lv_color_white()
#define THEME_HINT lv_color_hex(0x606060)
#define THEME_TRACK lv_color_hex(0xD0D0D0)
#endif
#define THEME_ACCENT lv_color_hex(0xD97757)  // orange for the dot and the spinner

#define INDICATOR_SIZE 44                    // the space kept for the dot or spinner, in pixels
#define DOT_SIZE 20                          // the orange dot's width and height
#define PAGE_DOT_SIZE 6                       // the small dot beside a paragraph
#define PAGE_DOT_X 6                         // where that dot sits, from the left edge
#define MAX_PAGE_DOTS 11                     // one page has 11 lines, so 11 dots at most
#define TEXT_X 16                            // text starts here, leaving a margin for the dots
#define TEXT_WIDTH (LCD_WIDTH - TEXT_X - 8)  // 8 px of margin on the right
#define PULSE_MS 700                         // how long the dot takes to fade one way

#define LOG_MAX_LINES 13
static char s_log[1024];

#define LCD_WIDTH 240
#define LCD_HEIGHT 280
#define LCD_RESET_WAIT_MS 120                // how long the screen needs after a reset
#define LOG_MAX_HEIGHT (LCD_HEIGHT - 10 - 30)


#define LVGL_TASK_MAX_DELAY_MS 50            // check often, so a new screen appears right away
#define LVGL_TASK_MIN_DELAY_MS (1000 / CONFIG_FREERTOS_HZ)
#define LVGL_TASK_STACK_SIZE   (6 * 1024)    // drawing the spinner needs more stack than plain text
#define LVGL_TASK_PRIORITY     2

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

// LVGL library is not thread-safe, mutex lock is necessary for calling LVGL APIs from different tasks
static _lock_t lvgl_api_lock;

static lv_obj_t *s_label;
static lv_obj_t *s_question;     // the orange question at the top of page 2
static lv_obj_t *s_page_dots[MAX_PAGE_DOTS];   // the small dots beside paragraphs
static lv_obj_t *s_page_label;   // the small "2/3" page counter
static lv_obj_t *s_status_box;   // holds everything on a status screen, like "Thinking"
static lv_obj_t *s_dot;          // the orange dot
static lv_obj_t *s_spinner;      // the orange spinning ring
static lv_obj_t *s_title;        // the big centered text
static lv_obj_t *s_hint;         // the small gray text under it



static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

// LVGL asks this whenever it wants to know the time, in milliseconds.
// Asking is better than a repeating timer: a timer would wake the CPU 500 times
// a second just to count, even when nothing on the screen is changing.
static uint32_t get_lvgl_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}


static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map){
    
    int w= area->x2 - area->x1 + 1;
    int h= area->y2 - area->y1 + 1;
    lv_draw_sw_rgb565_swap(px_map, w * h);
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2+1, area->y2+1, px_map);
    if (err != ESP_OK) {
        // The SPI send failed, so "done" will never come. Tell LVGL ourselves,
        // or it waits forever and the screen freezes.
        printf("LCD_FAIL: draw %s\n", esp_err_to_name(err));
        lv_display_flush_ready(disp);
    }
    
}

static void lvgl_port_task(void *arg)
{
    
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}


// Build the status screen once: indicator on top, big title, small hint.
// A "flex column" stacks its children and keeps them in the middle for us.
static void create_status_screen(void)
{
    // A see-through box as big as the whole screen.
    s_status_box = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_status_box);                  // no border or background
    lv_obj_set_size(s_status_box, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_flex_flow(s_status_box, LV_FLEX_FLOW_COLUMN);   // stack children top to bottom
    lv_obj_set_flex_align(s_status_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   // center them both ways
    lv_obj_set_style_pad_row(s_status_box, 12, 0);          // gap between the children
    lv_obj_add_flag(s_status_box, LV_OBJ_FLAG_HIDDEN);      // hidden until first used

    // A fixed-size holder, so the title doesn't jump when dot and spinner swap.
    lv_obj_t *holder = lv_obj_create(s_status_box);
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, INDICATOR_SIZE, INDICATOR_SIZE);

    // The dot: a small square with fully rounded corners.
    s_dot = lv_obj_create(holder);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_size(s_dot, DOT_SIZE, DOT_SIZE);
    lv_obj_center(s_dot);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, THEME_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);

    // The spinner: an orange arc moving around a dark ring.
    s_spinner = lv_spinner_create(holder);
    lv_obj_set_size(s_spinner, INDICATOR_SIZE, INDICATOR_SIZE);
    lv_spinner_set_anim_params(s_spinner, 1000, 90);        // one turn per second, arc is 90 degrees
    lv_obj_set_style_arc_width(s_spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, THEME_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spinner, THEME_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    // The big title.
    s_title = lv_label_create(s_status_box);
    lv_obj_set_width(s_title, LCD_WIDTH - 24);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);

    // The small gray hint under it.
    s_hint = lv_label_create(s_status_box);
    lv_obj_set_width(s_hint, LCD_WIDTH - 24);
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_hint, THEME_HINT, 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
}

esp_err_t app_display_init(void){
    
    spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 40 * sizeof(uint16_t),
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .spi_mode = 0,
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .pclk_hz = 10 * 1000 * 1000,   // 10 MHz: the speed verified working on these jumper wires
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,    
    };


    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    // The ST7789 can ignore commands for up to 120 ms after a reset. The driver
    // only waits 10 ms, so without this the screen can stay blank on some boots.
    vTaskDelay(pdMS_TO_TICKS(LCD_RESET_WAIT_MS));
    // Also reset it by command, so it starts clean even when only the chip restarted
    // (flashing, RESET button) and the screen kept its power and old state.
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, LCD_CMD_SWRESET, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(LCD_RESET_WAIT_MS));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 20));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_ERROR_CHECK(gpio_reset_pin(LCD_BL));
    ESP_ERROR_CHECK(gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LCD_BL, BL_ON)); // Turn the backlight on


    lv_init();
    // create a lvgl display
    lv_display_t *display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    size_t draw_buffer_sz = LCD_WIDTH * 20 * sizeof(lv_color16_t);

    void *buf1 = spi_bus_dma_memory_alloc(SPI2_HOST, draw_buffer_sz, 0);
    assert(buf1);
    void *buf2 = spi_bus_dma_memory_alloc(SPI2_HOST, draw_buffer_sz, 0);
    assert(buf2); //Bytes per buffer: 240 × 20 × 2 = 9.6 KB.
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, flush_cb);

    lv_tick_set_cb(get_lvgl_time_ms);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };

    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display));
    
    lv_obj_set_style_bg_color(lv_screen_active(), THEME_BG, 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(lv_screen_active(), THEME_FG, 0);
    // The screen never scrolls, so LVGL never draws a scrollbar on the right.
    lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(lv_screen_active(), LV_SCROLLBAR_MODE_OFF);

    s_label = lv_label_create(lv_screen_active());
    lv_obj_set_width(s_label, TEXT_WIDTH);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_label, LV_ALIGN_TOP_LEFT, TEXT_X, 8);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_label, "");

    // The question: same size as the answer, but orange. Hidden for now.
    s_question = lv_label_create(lv_screen_active());
    lv_obj_set_width(s_question, TEXT_WIDTH);
    lv_obj_align(s_question, LV_ALIGN_TOP_LEFT, TEXT_X, 8);
    lv_obj_set_style_text_font(s_question, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_question, THEME_ACCENT, 0);
    lv_obj_add_flag(s_question, LV_OBJ_FLAG_HIDDEN);

    // The paragraph dots: made once, then moved into place for each page.
    for (int i = 0; i < MAX_PAGE_DOTS; i++) {
        s_page_dots[i] = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(s_page_dots[i]);
        lv_obj_set_size(s_page_dots[i], PAGE_DOT_SIZE, PAGE_DOT_SIZE);
        lv_obj_set_style_radius(s_page_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_page_dots[i], THEME_ACCENT, 0);
        lv_obj_set_style_bg_opa(s_page_dots[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(s_page_dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    // The page counter, at the bottom middle, hidden for now.
    // 11 lines of 21 px end at 239 px down, so the bottom row is free for it,
    // and the middle stays clear of the rounded corners.
    s_page_label = lv_label_create(lv_screen_active());
    lv_obj_align(s_page_label, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_text_font(s_page_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_page_label, THEME_HINT, 0);
    lv_obj_add_flag(s_page_label, LV_OBJ_FLAG_HIDDEN);

    create_status_screen();   // ready to use, but hidden until the first status


    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);

    return ESP_OK;

}

// Hide every paragraph dot. Call this while holding the lock.
static void hide_page_dots(void)
{
    for (int i = 0; i < MAX_PAGE_DOTS; i++) {
        lv_obj_add_flag(s_page_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// Switch back to the plain text view, starting at the top left (the boot log).
// Call this while holding the lock.
static void show_text_view(void)
{
    lv_obj_add_flag(s_status_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_question, LV_OBJ_FLAG_HIDDEN);
    hide_page_dots();
    lv_obj_remove_flag(s_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_label, LV_ALIGN_TOP_LEFT, TEXT_X, 8);
}

void app_display_text(const char *text){
    _lock_acquire(&lvgl_api_lock);
    show_text_view();
    lv_label_set_text(s_label, text);
    _lock_release(&lvgl_api_lock);
}

void app_display_append(const char *text){
    _lock_acquire(&lvgl_api_lock);
    show_text_view();
    while ((strlen(s_log) + strlen(text) + 1) > sizeof(s_log)){
        char *n1 = strchr(s_log, '\n');
        if (n1 == NULL) { s_log[0] = '\0'; break; }
        memmove(s_log, n1 + 1, strlen(n1+1)+1);
    }
    strncat(s_log, text, sizeof(s_log) - strlen(s_log) - 1);

    int count_new_line = 0;

    for (int i = 0; s_log[i] != '\0'; i++){
        if (s_log[i] == '\n') { count_new_line++;}
    }
    while (count_new_line > LOG_MAX_LINES){
        char *n1 = strchr(s_log, '\n');
        if (n1 == NULL) { s_log[0] = '\0'; break; }
        memmove(s_log, n1 + 1, strlen(n1+1)+1);
        count_new_line--;
    }
    lv_label_set_text(s_label, s_log);
    lv_obj_update_layout(s_label);                    // let LVGL compute the new size now
    while (lv_obj_get_height(s_label) > LOG_MAX_HEIGHT) {
    char *n1 = strchr(s_log, '\n');
    if (n1 == NULL) break;                        // one giant line: nothing to drop
    memmove(s_log, n1 + 1, strlen(n1 + 1) + 1);   // drop the oldest line
    lv_label_set_text(s_label, s_log);
    lv_obj_update_layout(s_label);
    }
    _lock_release(&lvgl_api_lock);
}


void app_display_backspace(void){
    _lock_acquire(&lvgl_api_lock);
    size_t n = strlen(s_log);
    if (n > 0 && s_log[n - 1] != '\n') {   // never delete past the start of the line
        s_log[n - 1] = '\0';
        lv_label_set_text(s_label, s_log);
    }
    _lock_release(&lvgl_api_lock);
}


// Show an orange question (can be empty), the text under it with dots, and a counter like "2/3".
void app_display_page(const char *question, const char *text, const int *dot_lines, int dot_count, int page_number, int total_pages)
{
    _lock_acquire(&lvgl_api_lock);        // LVGL's own task also uses LVGL, so take turns
    show_text_view();                     // the status screen goes away, text starts top left
    lv_label_set_text(s_label, text);     // the page's lines

    if (question[0] != '\0') {
        // This page has a question: show it, and put the text right under it.
        lv_label_set_text(s_question, question);
        lv_obj_remove_flag(s_question, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(s_question);                // let LVGL work out its height now
        lv_obj_align_to(s_label, s_question, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    }

    // Put one dot beside each paragraph's first line, centered on that line.
    lv_obj_update_layout(s_label);                       // let LVGL work out where the text is
    int text_top = lv_obj_get_y(s_label);                // the top of the first text line
    int line_height = lv_font_get_line_height(&lv_font_montserrat_18);
    for (int i = 0; i < dot_count && i < MAX_PAGE_DOTS; i++) {
        int y = text_top + dot_lines[i] * line_height + (line_height - PAGE_DOT_SIZE) / 2;
        lv_obj_set_pos(s_page_dots[i], PAGE_DOT_X, y);
        lv_obj_remove_flag(s_page_dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (total_pages > 1) {
        // More than one page: show the counter.
        lv_label_set_text_fmt(s_page_label, "%d/%d", page_number, total_pages);
        lv_obj_remove_flag(s_page_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Only one page: no counter needed.
        lv_obj_add_flag(s_page_label, LV_OBJ_FLAG_HIDDEN);
    }
    _lock_release(&lvgl_api_lock);
}

// LVGL calls this over and over during the pulse, with the next see-through value.
static void set_dot_opacity(void *dot, int32_t opacity)
{
    lv_obj_set_style_bg_opa(dot, opacity, 0);
}

// Make the dot fade down and back up, forever.
static void start_dot_pulse(void)
{
    lv_anim_t anim;                                    // the settings for this animation
    lv_anim_init(&anim);                               // fill in the defaults
    lv_anim_set_var(&anim, s_dot);                     // the thing being animated
    lv_anim_set_exec_cb(&anim, set_dot_opacity);       // the function that changes it
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_30);   // from solid to mostly see-through
    lv_anim_set_duration(&anim, PULSE_MS);             // time to fade down
    lv_anim_set_reverse_duration(&anim, PULSE_MS);     // time to fade back up
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);   // never stop by itself
    lv_anim_start(&anim);
}

// Backlight off, and the screen's own chip to sleep. Its memory keeps the picture.
void app_display_screen_off(void)
{
    _lock_acquire(&lvgl_api_lock);            // so LVGL can't draw while the chip sleeps
    gpio_set_level(LCD_BL, BL_OFF);
    esp_lcd_panel_disp_sleep(panel_handle, true);
    _lock_release(&lvgl_api_lock);
}

// Wake the screen's chip, then the backlight. The old picture is still there.
void app_display_screen_on(void)
{
    _lock_acquire(&lvgl_api_lock);
    esp_lcd_panel_disp_sleep(panel_handle, false);   // this waits 100 ms for the chip
    vTaskDelay(pdMS_TO_TICKS(20));                   // the chip's datasheet asks for 120 ms in total
    gpio_set_level(LCD_BL, BL_ON);
    _lock_release(&lvgl_api_lock);
}

// Show a status screen: indicator, big title and small hint, all in the middle.
void app_display_status(const char *title, const char *hint, display_indicator_t indicator)
{
    _lock_acquire(&lvgl_api_lock);

    // Hide the other view, and forget any old text.
    s_log[0] = '\0';
    lv_label_set_text(s_label, "");
    lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_question, LV_OBJ_FLAG_HIDDEN);
    hide_page_dots();
    lv_obj_add_flag(s_page_label, LV_OBJ_FLAG_HIDDEN);

    // Stop the old pulse, and make the dot solid again.
    lv_anim_delete(s_dot, set_dot_opacity);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);

    if (indicator == DISPLAY_SPINNER) {
        // Busy: show the spinner instead of the dot.
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Otherwise: show the dot instead of the spinner.
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    }

    if (indicator == DISPLAY_PULSING_DOT) {
        start_dot_pulse();
    }

    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_remove_flag(s_status_box, LV_OBJ_FLAG_HIDDEN);

    _lock_release(&lvgl_api_lock);
}
