# AI Pin firmware

ESP-IDF firmware for the AI Pin. It records audio from the on-board PDM microphone, sends it to the relay over HTTPS, and shows the answer on an ST7789 display using LVGL.

## Board

[Seeed XIAO ESP32-S3 Sense](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) (target `esp32s3`), which has the PDM microphone on board. The board also has a camera, but this firmware doesn't use it. Pin assignments are in [`main/board.h`](main/board.h).

## Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) v6.1
- A running relay (see [`../relay/README.md`](../relay/README.md))

Dependencies (LVGL 9.3 and cJSON) are fetched automatically by the ESP-IDF Component Manager.

## Build and flash

```bash
cd firmware
cp main/secrets.example.h main/secrets.h     # then edit it
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

`<PORT>` is the board's serial port, for example `COM5` on Windows or `/dev/ttyACM0` on Linux.

## First boot

The pin asks for Wi-Fi over the USB serial console:

1. It scans and lists nearby networks.
2. Type the number of your network, then its password.
3. Once connected, it saves the network to flash (NVS) and skips this step on later boots.

## Using the pin

| Action | What it does |
|---|---|
| Hold | Record while held (up to 2 minutes) |
| Let go | Send the question |
| Tap on an answer | Next page |
| Hold 1.5 s on an answer | Clear it and go back to ready |
| Hold 8 s on an answer | Reboot |

## Code layout

Each file does one job. `main.c` only wires them together.

| File | Job |
|---|---|
| `main.c` | Start-up: Wi-Fi, clock, display, then the button loop |
| `board.c/.h` | Pin assignments, button setup and light sleep |
| `app_wifi.c`, `app_wifi_store.c` | Scan, connect, and remember one network in NVS |
| `app_time.c` | Set the clock over SNTP (needed for HTTPS certificates) |
| `app_mic.c` | Record 16 kHz, 16-bit audio from the PDM microphone |
| `app_relay.c` | Send text or audio to the relay and parse the JSON reply |
| `app_display.c` | ST7789 panel and LVGL set-up |
| `app_ui.c` | Button state machine and answer pages |
| `app_console.c`, `app_out.c` | Serial input and output helpers |

## Configuration

`main/secrets.h` (git-ignored) holds:

| Name | Meaning |
|---|---|
| `RELAY_URL` | Relay text endpoint, ending in `/v1/text` |
| `RELAY_VOICE_URL` | Relay audio endpoint, ending in `/v1/audio` |
| `RELAY_TOKEN` | Shared token sent in the `X-Device-Token` header |
