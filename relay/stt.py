r"""
Speech-to-text: run the Whisper models on one audio clip, then build the text for Claude.

    audio --> English model ---------> text in English ---------+
                                                                +--> Claude reads both
    audio --> second-language model --> text in that language --+

The second language is optional. Set SECOND_LANGUAGE to a Whisper language code
(for example "es", "hi" or "ar") and SECOND_LANGUAGE_MODEL to a Whisper model
that supports it. Leave SECOND_LANGUAGE empty for English only.
"""

# ---------------------------------------------------------------------------
# PART 1: IMPORTS (bring in code other people wrote)
# ---------------------------------------------------------------------------
import os                # tools for file paths and environment variables
import sys               # tools for this running program, like the command-line words

import numpy as np       # fast number arrays. "as np" is a short nickname for numpy

# Print text as UTF-8. Without this, non-Latin scripts break when saved to a file on Windows.
sys.stdout.reconfigure(encoding="utf-8")


# ---------------------------------------------------------------------------
# PART 2: HELP WINDOWS FIND THE NVIDIA GPU LIBRARIES
# pip put them inside our .venv folder, but Windows doesn't look there by itself.
# This must run BEFORE we import faster_whisper, because that import needs them.
# ---------------------------------------------------------------------------
# sys.prefix is the path of our .venv folder. os.path.join glues path pieces together.
nvidia_folder = os.path.join(sys.prefix, "Lib", "site-packages", "nvidia")

# Do the same steps for two libraries: first "cublas", then "cudnn".
for library in ["cublas", "cudnn"]:
    # Build the path of the folder holding this library's DLL files.
    dll_folder = os.path.join(nvidia_folder, library, "bin")

    # Only continue if that folder really exists on this PC.
    if os.path.isdir(dll_folder):
        # Tell Python: "you may load DLLs from this folder".
        os.add_dll_directory(dll_folder)
        # Also add the folder to PATH, the list of places Windows searches.
        os.environ["PATH"] = dll_folder + os.pathsep + os.environ["PATH"]

# WhisperModel is the speech model. decode_audio turns an audio file into numbers.
from faster_whisper import WhisperModel, decode_audio


# ---------------------------------------------------------------------------
# PART 3: SETTINGS
# ---------------------------------------------------------------------------
# Optional second language, as a Whisper language code such as "es", "hi" or "ar".
# Empty means English only.
SECOND_LANGUAGE = os.environ.get("SECOND_LANGUAGE", "").strip()

# The model for the second language: a Whisper size ("small", "medium", "large-v3")
# or a Hugging Face model ID fine-tuned for that language.
SECOND_MODEL_NAME = os.environ.get("SECOND_LANGUAGE_MODEL", "medium")

# The English-only model's name.
ENGLISH_MODEL_NAME = "medium.en"


# ---------------------------------------------------------------------------
# PART 4: LOADING A MODEL
# ---------------------------------------------------------------------------
def load_model(model_name, language):
    """Load one model. Try the GPU first. If that fails, use the CPU."""

    # "try" means: attempt this code. If anything fails, jump to "except" below.
    try:
        # Load the model onto the NVIDIA GPU ("cuda") using 16-bit decimal numbers.
        model = WhisperModel(model_name, device="cuda", compute_type="float16")

        # Make 1 second of silence: 16000 samples, all zero.
        silence = np.zeros(16000, dtype=np.float32)

        # Test the GPU now. Missing DLLs often only fail on the first real run.
        # model.transcribe gives back 2 things. We keep the first and ignore the second.
        segments, ignored_info = model.transcribe(silence, language=language)

        # Looping over segments is what makes the GPU really do the work.
        for segment in segments:
            pass  # "pass" means do nothing. We only wanted to run the test.

        # Reaching this line means the GPU works.
        print(f"STT_DEVICE: cuda ({model_name}, float16)")

    # We land here only if something in the "try" block failed.
    except Exception as error:
        # Say what went wrong.
        print(f"GPU not usable ({error}); falling back to CPU")

        # Load the same model on the CPU instead. Slower, but it still works.
        model = WhisperModel(model_name, device="cpu", compute_type="int8")
        print(f"STT_DEVICE: cpu ({model_name}, int8)")

    # Hand the loaded model back to whoever called this function.
    return model


# ---------------------------------------------------------------------------
# PART 5: TURNING AUDIO INTO TEXT
# ---------------------------------------------------------------------------
def transcribe(model, audio, language):
    """Run one model on the audio and return what it heard, as one string."""

    # Set up the job. No real work happens yet: "segments" is a generator,
    # which is a promise to work out each piece of text when we ask for it.
    segments, ignored_info = model.transcribe(
        audio,                     # the audio samples
        language=language,         # the language to write in, for example "en"
        beam_size=5,               # try 5 guesses at once and keep the best (more accurate)
        vad_filter=False,          # keep off: the silence filter cut off the first syllable
        without_timestamps=True,   # we only want the words, not when they were said
    )

    # Start with empty text, then add each piece to it.
    full_text = ""

    # THE REAL GPU WORK HAPPENS HERE: each loop computes the next piece of text.
    for segment in segments:
        # .strip() removes spaces at the start and end. + " " adds one space between pieces.
        full_text = full_text + segment.text.strip() + " "

    # Remove the extra space at the very end, then hand the text back.
    return full_text.strip()


# ---------------------------------------------------------------------------
# PART 6: BUILDING THE MESSAGE FOR CLAUDE
# ---------------------------------------------------------------------------
def build_claude_prompt(second_text, english_text):
    """Put both transcripts into the one message Claude will read."""

    # Each \n means "start a new line". The rules for reading this format
    # are in "Voice Input Rules" in ~/assistant-brain/CLAUDE.md
    prompt = "[Voice]\n"
    if SECOND_LANGUAGE:
        prompt = prompt + "Second-language model (" + SECOND_LANGUAGE + "): " + second_text + "\n"
    prompt = prompt + "English model: " + english_text

    # Hand the finished message back.
    return prompt


