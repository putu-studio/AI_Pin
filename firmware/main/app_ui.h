#pragma once   // include this file only once, even if two files ask for it

// Runs the button state machine: Ready, Listening, Sending, Thinking, Answer.
// It loops forever, so call it as the last line of app_main.
void app_ui_run(void);
