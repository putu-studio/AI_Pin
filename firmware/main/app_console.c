#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "app_console.h"
#include "app_out.h"
#include "app_display.h"

void app_console_init(){
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
}

static bool read_line_ex(char *buf, size_t size, bool mask){
    size_t len = 0;
    while (1) {
        int c = fgetc(stdin);                 // wait for ONE key
        if (c == EOF) continue;

        if (c == '\n') {                      // Enter
            putchar('\n'); 
            app_display_append("\n");
            fflush(stdout);
            buf[len] = '\0';                  // end the string
            return len > 0;                   // empty line = false
        }
        if (c == 0x08 || c == 0x7F) {         // Backspace
            if (len > 0) {
                len--;
                fputs("\b \b", stdout);       // back, blank, back
                fflush(stdout);
                app_display_backspace();
            }
            continue;
        }
        if (c >= 32 && c <= 126 && len < size - 1) {   // printable + room left
            buf[len++] = (char)c;
            putchar(mask ? '*' : c);          // show the key (or a star)
            char s[2] = { mask ? '*' : (char)c, '\0' };   // a 1-letter string
            app_display_append(s);
            fflush(stdout);
        }
    }
}

bool app_console_read_line(char *buf, size_t size) { return read_line_ex(buf, size, false); }

bool app_console_read_password(char *buf, size_t size) { return read_line_ex(buf, size, true); }




int app_console_read_number(const char *prompt, int min, int max){
    char text[INPUT_TEXT_SIZE];

    while (true){
        out_printf("%s (%d-%d): ", prompt, min, max);
        fflush(stdout);
        if (!app_console_read_line(text, sizeof(text))){
            continue; 
        }
        char *end;
        long n = strtol(text, &end, 10);

        if (end != text && *end == '\0' && n>=min && n<=max){
            return (int)(n);
        } else{
            out_printf("Please type a valid number within the range.\n");
        }
        continue;
    }

}
