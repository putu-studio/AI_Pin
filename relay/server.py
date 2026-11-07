import os
from fastapi import FastAPI, Header, HTTPException
from pydantic import BaseModel
from claude_runner import ask_claude


import asyncio                # lets slow work run without freezing the server
import numpy as np            # for converting the audio numbers
from fastapi import Request   # gives us access to the raw request

from stt import load_model, transcribe, build_claude_prompt
from stt import SECOND_LANGUAGE, SECOND_MODEL_NAME, ENGLISH_MODEL_NAME

# 2 minutes of audio is 3,840,000 bytes, so this leaves a little room.
MAX_AUDIO_BYTES = 4000000


# Loading takes about 5 s, so do it once at startup, not on every question.
english_model = load_model(ENGLISH_MODEL_NAME, "en")

# The second-language model is optional (see SECOND_LANGUAGE in .env.example).
second_model = load_model(SECOND_MODEL_NAME, SECOND_LANGUAGE) if SECOND_LANGUAGE else None

# No public documentation pages: strangers shouldn't get a list of our endpoints.
app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
DEVICE_TOKEN = os.environ.get("DEVICE_TOKEN", "")

class Ask(BaseModel):
    text: str

@app.get("/ping")
def ping():
    return {"ok": True}

@app.post("/v1/text")
def text_query(body: Ask, x_device_token: str = Header(None)):
    if not DEVICE_TOKEN or x_device_token != DEVICE_TOKEN:
        raise HTTPException(401)
    try: 
        return ask_claude(body.text)
    except Exception: 
        return dict (kind = "error", title = "Error", lines = ["Try again"])

@app.post("/v1/audio")
async def audio_query(request: Request, x_device_token: str = Header(None)):

    # Same token check as /v1/text: only our pin may use this.
    if not DEVICE_TOKEN or x_device_token != DEVICE_TOKEN:
        raise HTTPException(401)

    # Read the raw bytes the pin sent. "await" means: wait here, but let
    # the server handle other requests in the meantime.
    audio_bytes = await request.body()

    # No audio at all means the request is wrong. 400 = bad request.
    if len(audio_bytes) == 0:
        raise HTTPException(400)

    # Too much audio. 413 means "too large".
    if len(audio_bytes) > MAX_AUDIO_BYTES:
        raise HTTPException(413)

    # Read the bytes as 16-bit whole numbers, like the pin's int16_t.
    samples = np.frombuffer(audio_bytes, dtype=np.int16)

    # Whisper wants decimals from -1.0 to 1.0, so divide by 32768.
    audio = samples.astype(np.float32) / 32768

    # Run each model on a separate thread, so the server doesn't freeze.
    english_text = await asyncio.to_thread(transcribe, english_model, audio, "en")
    second_text = ""
    if second_model is not None:
        second_text = await asyncio.to_thread(transcribe, second_model, audio, SECOND_LANGUAGE)

    # Nothing heard by either model: no need to ask Claude.
    if second_text == "" and english_text == "":
        return dict(kind="error", title="Didn't catch that", lines=["Please try again"])

    # Put both transcripts into one message for Claude.
    prompt = build_claude_prompt(second_text, english_text)

    # Ask Claude. This is slow, so it also runs on a worker thread.
    try:
        answer = await asyncio.to_thread(ask_claude, prompt)
        return answer
    except Exception:
        # Same friendly error as /v1/text, so the pin handles both the same way.
        return dict(kind="error", title="Error", lines=["Try again"])





