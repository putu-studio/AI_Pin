// secrets.example.h: copy this file to secrets.h and fill in your own values.
// secrets.h is git-ignored, so your relay address and token stay on your machine.
#pragma once

// HTTPS address of your relay server (see relay/README.md).
#define RELAY_URL        "https://your-relay.example/v1/text"
#define RELAY_VOICE_URL  "https://your-relay.example/v1/audio"

// Must match DEVICE_TOKEN in the relay's .env file. Use a long random string.
#define RELAY_TOKEN      "change-me"
