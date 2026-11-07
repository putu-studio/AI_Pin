// app_ui.c: the button state machine and the answer pages.
// One button does everything, because what it does depends on the state.

#include <stdio.h>              // printf
#include <stdbool.h>            // bool, true, false
#include <string.h>             // strlcat
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"      // vTaskDelay
#include "esp_system.h"         // esp_restart
#include "esp_timer.h"          // esp_timer_get_time, a clock in microseconds
#include "app_ui.h"
#include "app_mic.h"
#include "app_relay.h"
#include "app_display.h"
#include "app_wifi.h"
#include "driver/usb_serial_jtag.h"   // usb_serial_jtag_is_connected
#include "board.h"              // board_button_pressed

#define BUMP_MS 300             // shorter than this is an accidental press
#define LONG_PRESS_MS 1500      // hold this long on an answer to clear it
#define REBOOT_MS 8000          // hold this long on an answer to reboot
#define POLL_MS 20              // how often we check the button
#define LINES_PER_PAGE 11       // must match LINES_PER_PAGE in relay/claude_runner.py
#define WIFI_WAIT_MS 15000      // how long to wait for Wi-Fi after you let go
#define SLEEP_AFTER_MS 1000     // nothing happening for this long: the chip can sleep
#define SCREEN_OFF_READY_MS 10000    // screen timeout on the Ready screen
#define SCREEN_OFF_ANSWER_MS 45000   // longer on an answer, because you're reading it

// The 4 states. The device is always in exactly one of them.
typedef enum {
    ST_READY,       // waiting for you to press
    ST_RECORDING,   // button held, recording
    ST_THINKING,    // uploading and waiting for Claude
    ST_ANSWER,      // showing the answer
} ui_state_t;

static relay_answer_t answer;   // static: about 1.4 KB, too big for the stack
static int page = 0;            // which page we're showing, counted from 0


// How many pages this answer needs. Adding (LINES_PER_PAGE - 1) makes the
// integer division round up: 12 lines at 11 per page = (12 + 10) / 11 = 2 pages.
static int page_count(void)
{
    return (answer.count + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
}

// Add one line to some text, with a line break between lines but none at the end.
static void add_line(char *text, size_t size, const char *line)
{
    if (text[0] != '\0') {            // already has a line in it
        strlcat(text, "\n", size);
    }
    strlcat(text, line, size);
}

// Draw the current page.
static void show_page(void)
{
    static char text[LINES_PER_PAGE * ANSWER_LINE_SIZE];   // static: keep it off the stack
    static char question[LINES_PER_PAGE * ANSWER_LINE_SIZE];   // the orange part, also off the stack
    text[0] = '\0';                                        // start with empty text
    question[0] = '\0';

    // This page's first line, and the line just after its last.
    int first = page * LINES_PER_PAGE;
    int end = first + LINES_PER_PAGE;
    if (end > answer.count) {
        end = answer.count;          // the last page may be shorter
    }

    // The question only appears at the top of page 2, which is page number 1 here.
    int question_end = first;
    if (page == 1) {
        question_end = first + answer.question_lines;
    }

    int dot_lines[LINES_PER_PAGE];   // which text lines on this page get a dot
    int dot_count = 0;               // how many dots this page has
    int text_line = 0;               // the line number inside the text below the question

    // Join this page's lines: question lines into one text, the rest into the other.
    for (int i = first; i < end; i++) {
        if (i < question_end) {
            add_line(question, sizeof(question), answer.lines[i]);
        } else {
            if (answer.starts_paragraph[i]) {
                dot_lines[dot_count] = text_line;
                dot_count = dot_count + 1;
            }
            text_line = text_line + 1;
            add_line(text, sizeof(text), answer.lines[i]);
        }
    }

    int total = page_count();
    app_display_page(question, text, dot_lines, dot_count, page + 1, total);     // people count from 1, not 0
    printf("PAGE: %d/%d\n", page + 1, total);
}

// Move to the next page, going back to the first after the last.
static void next_page(void)
{
    page = page + 1;
    if (page >= page_count()) {
        page = 0;
    }
    show_page();
}

// Clear the screen and show the Ready message.
static void show_ready(void)
{
    app_display_status("Ready", "Hold to talk", DISPLAY_STILL_DOT);
}

// Upload what was recorded, and return the state to go to next.
static ui_state_t send_recording(void)
{
    app_mic_stop_recording();
    printf("REC: %u bytes\n", (unsigned)app_mic_get_length());

    // app_relay_voice changes this to "Thinking" once the upload is done.
    printf("UI: SENDING\n");
    app_display_status("Sending", "Uploading your voice", DISPLAY_SPINNER);

    // Normally Wi-Fi started connecting while you were talking, and this is
    // quick. It's only still off if you let go right after the bump time.
    app_wifi_turn_on();
    esp_err_t err = ESP_FAIL;
    if (app_wifi_wait_connected(WIFI_WAIT_MS)) {
        err = app_relay_voice(app_mic_get_audio(), app_mic_get_length(), &answer);
    } else {
        printf("VOICE_FAIL: no Wi-Fi\n");
    }

    // Finished with the network, whichever way it went.
    app_wifi_turn_off();

    if (err != ESP_OK) {
        printf("UI: READY\n");
        app_display_status("Can't reach relay", "Is the PC on?\nHold to try again", DISPLAY_STILL_DOT);
        return ST_READY;
    }

    printf("VOICE_OK\n");
    page = 0;               // every new answer starts on page 1
    show_page();
    printf("UI: ANSWER\n");
    return ST_ANSWER;
}

void app_ui_run(void)
{
    ui_state_t state = ST_READY;
    bool was_pressed = false;       // the button, the last time we looked
    int64_t press_start = 0;        // when the button went down, in microseconds
    bool pressed_on_answer = false; // did this press start on the Answer screen?
    bool screen_on = true;
    bool press_woke_screen = false; // this press only lights the screen, not a page turn
    int64_t last_activity = esp_timer_get_time();   // when something last happened

    app_wifi_turn_off();            // Wi-Fi was only needed at boot, to set the clock
    show_ready();
    printf("UI: READY\n");

    while (1) {
        bool now = board_button_pressed();
        int64_t time_now = esp_timer_get_time();

        // Compare with the last loop to catch the moment things change.
        bool just_pressed = now && !was_pressed;
        bool just_released = !now && was_pressed;

        if (just_pressed) {
            press_start = time_now;
        }

        // How long it has been held, in milliseconds.
        int held_ms = (int)((time_now - press_start) / 1000);

        switch (state) {

        case ST_READY:
            // Still holding the press that cleared the answer: a very long hold reboots.
            if (pressed_on_answer && now && held_ms >= REBOOT_MS) {
                printf("UI: REBOOT\n");
                esp_restart();
            }
            if (just_pressed) {
                // Record right away, so your first word isn't lost.
                // The mic comes first, because waking the screen takes 120 ms.
                app_mic_start_recording();
                if (!screen_on) {
                    app_display_screen_on();
                    screen_on = true;
                }
                app_display_status("Listening", "Release to send", DISPLAY_PULSING_DOT);
                printf("UI: LISTENING\n");
                state = ST_RECORDING;
            }
            break;

        case ST_RECORDING:
            // Held long enough to be real speech: connect while you talk, so the
            // upload can start as soon as you let go.
            if (now && held_ms >= BUMP_MS) {
                app_wifi_turn_on();
            }
            if (just_released && held_ms < BUMP_MS) {
                // Too short to be speech: a bump, so throw it away.
                app_mic_stop_recording();
                show_ready();
                printf("UI: READY (bump)\n");
                state = ST_READY;
            } else if (just_released || app_mic_is_full()) {
                state = ST_THINKING;
            }
            break;

        case ST_THINKING:
            // The upload blocks until the answer arrives, so presses
            // during this time do nothing.
            state = send_recording();
            // Start timing fresh. Otherwise, if you're still holding after a
            // full buffer, that long hold would count as a reboot on the answer.
            press_start = esp_timer_get_time();
            last_activity = press_start;   // the answer just arrived, so restart the screen timeout
            now = board_button_pressed();
            break;

        case ST_ANSWER:
            if (just_pressed) {
                pressed_on_answer = true;
                press_woke_screen = !screen_on;
                if (!screen_on) {
                    app_display_screen_on();
                    screen_on = true;
                }
            }
            if (pressed_on_answer && now && held_ms >= LONG_PRESS_MS) {
                // Long press: clear the answer right away, without waiting for release.
                show_ready();
                printf("UI_RESET\n");
                state = ST_READY;
            } else if (just_released && !press_woke_screen) {
                // Tap: next page.
                next_page();
            }
            break;
        }

        // Remember for the next loop.
        was_pressed = now;
        if (!now) {
            pressed_on_answer = false;   // the button is up, so that press is over
        }

        // Power saving. Only Ready and Answer ever sit waiting for you.
        bool waiting = (state == ST_READY || state == ST_ANSWER);
        if (now || !waiting) {
            last_activity = esp_timer_get_time();
        }
        int idle_ms = (int)((esp_timer_get_time() - last_activity) / 1000);

        int screen_off_ms = SCREEN_OFF_READY_MS;
        if (state == ST_ANSWER) {
            screen_off_ms = SCREEN_OFF_ANSWER_MS;
        }

        if (waiting && screen_on && idle_ms >= screen_off_ms) {
            app_display_screen_off();
            screen_on = false;
            printf("SCREEN: OFF\n");
        }

        // Light sleep stops the USB connection, so don't sleep while plugged into a
        // computer, or the serial port would keep dropping. A charger or battery doesn't count.
        if (waiting && idle_ms >= SLEEP_AFTER_MS && !usb_serial_jtag_is_connected()) {
            int sleep_ms = 0;                    // screen off: sleep until the button
            if (screen_on) {
                sleep_ms = screen_off_ms - idle_ms;   // screen on: wake up to turn it off
            }
            board_sleep_until_button(sleep_ms);
            continue;                            // check the button again straight away
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
