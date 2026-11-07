# AI Pin relay

A small FastAPI server that the pin talks to. It turns speech into text with Whisper, asks an AI assistant using the instructions in your brain folder, and returns an answer formatted for the pin's screen.

## How a question is answered

1. The pin sends raw 16 kHz, 16-bit audio to `POST /v1/audio` (or text to `POST /v1/text`).
2. Whisper transcribes it in English, and in a second language too if you set one up (see [Supporting other languages](#supporting-other-languages)).
3. The transcripts go to the assistant, which runs in your brain folder and can read your notes and save new memories there.
4. If the question needs current information, the assistant asks for a web search, or for weather, which is fetched directly from [Open-Meteo](https://open-meteo.com).
5. The reply is checked against [`schema.json`](schema.json) and split into screen pages.

## Requirements

- Python 3.10 or later
- [Claude Code](https://docs.anthropic.com/en/docs/claude-code) installed and signed in on the same computer. The relay runs it with `claude -p`
- An NVIDIA GPU is optional. Whisper falls back to the CPU, which is slower
- A way to reach the relay over HTTPS from the pin, for example a tunnel or reverse proxy that gives the relay an HTTPS address with a valid certificate

## Set up

```bash
cd relay
python -m venv .venv
# Windows: .venv\Scripts\activate    macOS/Linux: source .venv/bin/activate
pip install -r requirements.txt          # or requirements-gpu.txt for an NVIDIA GPU

cp .env.example .env          # then set DEVICE_TOKEN (and SECOND_LANGUAGE if you want one)
cp -r ../brain-template ~/assistant-brain
```

The first run downloads the Whisper models from Hugging Face, so it takes a few minutes.

## Run

```bash
# load DEVICE_TOKEN from .env into your shell first, then:
uvicorn server:app --host 0.0.0.0 --port 8000
```

Check it's up:

```bash
curl http://localhost:8000/ping        # {"ok": true}
```

Then expose port 8000 over HTTPS and put that address in the pin's `secrets.h`.

## Endpoints

| Method | Path | Body | Auth |
|---|---|---|---|
| `GET` | `/ping` | none | none |
| `POST` | `/v1/text` | `{"text": "..."}` | `X-Device-Token` header |
| `POST` | `/v1/audio` | raw int16 PCM, 16 kHz mono, up to 2 minutes | `X-Device-Token` header |

Requests without the right token get `401`. The interactive API docs are turned off on purpose.

## Supporting other languages

The relay always runs an English Whisper model (`medium.en`). It can also run a second model for one more language, side by side on the same audio. The assistant gets both transcripts and uses whichever one makes sense, so you can ask in either language, or mix the two in one sentence.

Set these in `.env`:

| Variable | Meaning | Example |
|---|---|---|
| `SECOND_LANGUAGE` | A [Whisper language code](https://github.com/openai/whisper/blob/main/whisper/tokenizer.py). Leave it empty for English only | `es` |
| `SECOND_LANGUAGE_MODEL` | The model for that language: a Whisper size such as `small`, `medium` or `large-v3`, or the ID of a fine-tuned model on [Hugging Face](https://huggingface.co/models?pipeline_tag=automatic-speech-recognition) | `medium` |

Tips:

- The multilingual Whisper models work for many languages out of the box. For a language with fewer training hours, a model fine-tuned for it is usually much more accurate. Search Hugging Face for "whisper" plus the language name, and pick one in CTranslate2 format (`faster-whisper` loads only that format; others can be converted with `ct2-transformers-converter`).
- Each extra model needs memory and adds to the time per question. On a CPU, `small` is a good start.
- Answers stay in printable ASCII, because that is all the pin's screen font can draw, and `claude_runner.py` rejects anything else. Showing other scripts on the pin needs a font with those glyphs in the firmware, plus changes to those checks and to the rules in your brain folder's `CLAUDE.md`.

## Files

| File | Job |
|---|---|
| `server.py` | HTTP endpoints, token check, audio decoding |
| `stt.py` | Loads the Whisper models, transcribes audio, and builds the voice prompt |
| `claude_runner.py` | Runs the assistant, validates replies, handles web search and paging |
| `weather.py` | Fetches forecasts from Open-Meteo (no API key needed) |
| `schema.json` | The JSON shape every answer must follow |
