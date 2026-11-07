#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#define ANSWER_MAX_LINES 55    // 5 pages of 11 lines (the relay never sends more)
#define ANSWER_LINE_SIZE 27    // room for 26 characters + the end mark; the relay sends at most 21

esp_err_t app_relay_ask(const char *question);


// A box that holds one answer, ready for the screen.
typedef struct {
    char lines[ANSWER_MAX_LINES][ANSWER_LINE_SIZE];
    int count;
    int question_lines;   // how many lines at the top of page 2 are the question
    bool starts_paragraph[ANSWER_MAX_LINES];   // true = this line gets an orange dot
} relay_answer_t;


esp_err_t app_relay_voice(const int16_t *audio, size_t length, relay_answer_t *answer);

