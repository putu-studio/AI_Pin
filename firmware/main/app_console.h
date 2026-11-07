#pragma once

#include <stdbool.h>
#include <stddef.h>

#define INPUT_TEXT_SIZE 64

void app_console_init(void);
bool app_console_read_line(char *buf, size_t size);
bool app_console_read_password(char *buf, size_t size);
int app_console_read_number(const char *prompt, int min, int max);
