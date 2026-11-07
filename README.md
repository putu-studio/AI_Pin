# AI Pin

A wearable voice assistant built on the Seeed XIAO ESP32-S3 Sense. Hold the button, ask a question out loud, and the answer appears on a small screen, a page at a time.

The pin records your voice and sends it to a relay server that you run on your own computer. The relay turns speech into text, asks an AI assistant, and sends back a short answer sized for the screen.

The Sense board also has a camera, but this project doesn't use it. Only the on-board microphone is needed.

```
  ┌──────────────┐   Wi-Fi + HTTPS    ┌─────────────────────────────┐
  │   AI Pin     │ ─── audio ───────► │  Relay (your computer)      │
  │ ESP32-S3     │                    │  Whisper speech-to-text     │
  │ mic, screen, │ ◄── answer ─────── │  AI assistant + your brain  │
  │ one button   │   (JSON pages)     │  folder of instructions     │
  └──────────────┘                    └─────────────────────────────┘
```

## Features

- One-button control: hold to talk, let go to send, tap to turn pages, hold to clear.
- Speech-to-text with Whisper, in English plus one optional second language (see [Supporting other languages](relay/README.md#supporting-other-languages)).
- Answers are formatted for a 1.69" 240 × 280 display: 21 characters per line, 11 lines per page.
- Remembers things you ask it to remember, in a local folder you control.
- Web search and live weather (Open-Meteo, no API key) when a question needs current information.
- Wi-Fi set up over USB serial on first boot, then saved to flash.
- Custom carrier PCB with LiPo charging over USB-C.

## Repository layout

| Folder | What's inside |
|---|---|
| [`firmware/`](firmware/) | ESP-IDF firmware for the pin |
| [`relay/`](relay/) | Python server that the pin talks to |
| [`brain-template/`](brain-template/) | Starter instructions for the assistant. Copy it and make it your own |
| [`hardware/`](hardware/) | KiCad carrier board, footprints, BOM and Gerbers |

## Quick start

1. **Build the hardware.** Order the carrier board from [`hardware/`](hardware/), or wire a XIAO ESP32-S3 Sense to an ST7789 display and a push button on a breadboard (pins are in [`firmware/main/board.h`](firmware/main/board.h)).
2. **Set up the relay** on a computer that stays on. Follow [`relay/README.md`](relay/README.md). You'll end up with an HTTPS address and a token.
3. **Create your brain folder** by copying [`brain-template/`](brain-template/) to `~/assistant-brain` and editing `CLAUDE.md`.
4. **Flash the firmware.** Copy `firmware/main/secrets.example.h` to `secrets.h`, add the relay address and token, then build and flash. See [`firmware/README.md`](firmware/README.md).

On first boot, open the serial monitor, pick your Wi-Fi network and type its password. The pin remembers it after that.

## Status

In progress. The carrier PCB has been fabricated. Firmware and relay are under active development.

## License

[MIT](LICENSE) for the firmware, relay and hardware design files.
