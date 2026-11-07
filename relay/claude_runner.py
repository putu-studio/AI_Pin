import json
from datetime import datetime
import os
import subprocess
from pathlib import Path
import time
import textwrap   # splits long text into lines of a set width

from weather import get_weather_text

LINES_PER_PAGE = 11   # must match LINES_PER_PAGE in the pin's app_ui.c
LINE_WIDTH = 21   # most characters on one screen line; must fit Montserrat 18 on the pin
MORE_MAX_LINES = 40   # 5 pages = 55 lines: 11 short + 4 for Q: + 40 details
SESSION_TIMEOUT = 300          # 5 min of silence = new chat
_last = {"id": None, "time": 0.0}
_last_search = {"query": None, "time": 0.0}


PROJECT_DIR = Path(__file__).resolve().parent.parent
BRAIN_DIR = Path.home() / "assistant-brain"
MEMORIES_DIR = BRAIN_DIR / "memories"
SCHEMA_PATH = PROJECT_DIR / "relay" / "schema.json"


def validate_response(data, schema):
    """Validate the fields and display limits we rely on."""
    if not isinstance(data, dict):
        raise ValueError("Response must be a JSON object")

    if not {"kind", "title", "lines"} <= set(data) <= {"kind", "title", "lines", "query", "more", "asked"}:
        raise ValueError("Unexpected response fields")

    if data["kind"] not in {"answer", "list", "saved", "error", "needs_web"}:
        raise ValueError("Invalid response kind")

    if not isinstance(data["title"], str):
        raise ValueError("Title must be text")

    lines = data["lines"]
    if not isinstance(lines, list) or len(lines) > LINES_PER_PAGE:
        raise ValueError("Invalid lines array")

    more = data.get("more", [])
    if not isinstance(more, list) or len(more) > MORE_MAX_LINES:
        raise ValueError("Invalid more array")

    asked = data.get("asked", "")
    if not isinstance(asked, str) or len(asked) > 50:
        raise ValueError("Invalid asked text")

    for c in asked:
        if ord(c) < 32 or ord(c) > 126:
            raise ValueError("Only printable ASCII is allowed")

    for line in [data["title"], *lines, *more]:
        if not isinstance(line, str) or len(line) > LINE_WIDTH:
            raise ValueError("Text exceeds display limits")

        if any(ord(c) < 32 or ord(c) > 126 for c in line):
            raise ValueError("Only printable ASCII is allowed")

    if len(data["title"]) > 24:
        raise ValueError("Title exceeds 24 characters")

    return data

schema = json.loads(
        SCHEMA_PATH.read_text(encoding="utf-8")
    )

brain = BRAIN_DIR.as_posix()
memories = MEMORIES_DIR.as_posix()

COMMON = ["--output-format", "json", "--json-schema", json.dumps(schema),
          "--permission-mode", "dontAsk", "--strict-mcp-config", "--model", "sonnet"]


def mode_args(mode):
    if mode == "web":
        return ["--tools", "WebSearch,WebFetch", "--allowedTools", "WebSearch", "WebFetch", "--max-turns", "15"]
    if mode == "deep":
        return ["--tools", "WebSearch,WebFetch", "--allowedTools", "WebSearch", "WebFetch", "--max-turns", "30"]
    return [      "--tools", "Read,Write,Edit",
            "--allowedTools",
            f"Read({brain}/**)",
            f"Write({memories}/**)",
            f"Edit({memories}/**)",
            "--max-turns", "6",
           ]


def remove_blank_lines(lines):
    """Return the same lines without the empty ones, so the text stays compact."""
    kept = []
    for line in lines:
        if line.strip() != "":
            kept.append(line)
    return kept


def find_paragraph_starts(lines):
    """Return which lines start a paragraph, counted as if blank lines were already gone."""
    starts = []
    kept_count = 0
    after_blank = True   # the very first line starts a paragraph too
    for line in lines:
        if line.strip() == "":
            after_blank = True
        else:
            if after_blank:
                starts.append(kept_count)
            kept_count = kept_count + 1
            after_blank = False
    return starts


def join_pages(response, asked):
    """Short answer on page 1, then Q: and details from page 2."""
    # Take "more" and "asked" out of the reply. The pin only reads "lines".
    raw_more = response.pop("more", [])
    response.pop("asked", None)

    # Blank lines mark paragraphs. Note where each one starts, then remove the blanks.
    paragraph_lines = find_paragraph_starts(response["lines"])
    more_starts = find_paragraph_starts(raw_more)
    response["lines"] = remove_blank_lines(response["lines"])
    more = remove_blank_lines(raw_more)
    response["paragraph_lines"] = paragraph_lines   # the pin puts an orange dot beside these

    # No details: nothing to do, the answer fits on page 1, with no Q:.
    if len(more) == 0:
        return response

    # Pad the short answer with blank lines until it fills page 1 exactly.
    lines = response["lines"]
    while len(lines) < LINES_PER_PAGE:
        lines.append("")

    # Top of page 2: the question, split into lines that fit the screen.
    if asked != "":
        question_lines = textwrap.wrap("Q: " + asked, LINE_WIDTH)
        for line in question_lines:
            lines.append(line)
        response["question_lines"] = len(question_lines)   # the pin draws these in orange

    # The details start here, so their paragraph numbers move down by this much.
    first_detail_line = len(lines)
    for start in more_starts:
        paragraph_lines.append(first_detail_line + start)

    # Then each detail line.
    for line in more:
        lines.append(line)

    return response





def ask_claude(prompt):

    if not BRAIN_DIR.is_dir():
        raise RuntimeError(f"Brain folder not found: {BRAIN_DIR}")

    if not (BRAIN_DIR / "CLAUDE.md").is_file():
        raise RuntimeError("CLAUDE.md not found in brain folder")

    MEMORIES_DIR.mkdir(exist_ok=True)

    if not prompt or len(prompt) > 4000:
        raise ValueError("Prompt is empty or too long")

    # Memory run first. If Claude needs the web, it returns only a search
    # topic, and the web run sees that topic and nothing else.
    now = datetime.now().astimezone().strftime("%A %Y-%m-%d %H:%M %Z")
    memory_prompt = f"[Current local time: {now}]\n"

    # The memory run never sees the web run, so tell it what was last searched.
    # Only our own search words go in, never web page text, because the memory
    # run can write files and web text can't be trusted.
    if _last_search["query"] and time.time() - _last_search["time"] < SESSION_TIMEOUT:
        memory_prompt += f"[Your last web search was for: {_last_search['query']}]\n"

    response = run_claude("memory", memory_prompt + prompt)
    asked = response.get("asked", "")


    if response["kind"] != "needs_web":
        return join_pages(response, asked)


    query = response.get("query", "").strip()
    if not query:
        raise ValueError("needs_web reply without a query")

    # Weather is common and web search is slow, so fetch the numbers directly and
    # let Claude write the answer. This resumes the same chat, so Claude still
    # knows the question. If the weather service is down, use web search instead.
    if query.lower().startswith("weather:"):
        city = query[len("weather:"):].strip()
        try:
            weather_text = get_weather_text(city)
            weather_answer = run_claude("memory", f"[Weather data from Open-Meteo]\n{weather_text}")
            return join_pages(weather_answer, asked)
        except OSError:
            query = city + " weather forecast"

    # "deep:" means you said yes to trying harder, so allow a much longer search.
    mode = "web"
    label = ""
    if query.lower().startswith("deep:"):
        query = query[len("deep:"):].strip()
        mode = "deep"
        label = "[Deep search]\n"

    _last_search["query"] = query
    _last_search["time"] = time.time()

    # The web run starts fresh, so it needs today's date too.
    response = run_claude(mode, f"[Current local time: {now}]\n{label}{query}")
    if response["kind"] == "needs_web":
        raise RuntimeError("Web run asked for the web again")
    return join_pages(response, asked)


def run_claude(mode, prompt):
    args = ["claude", "-p"] + COMMON + mode_args(mode)

    if mode == "memory" and _last["id"] and time.time() - _last["time"] < SESSION_TIMEOUT:
        args += ["--resume", _last["id"]]

    env = os.environ.copy()
    env["CLAUDE_CODE_DISABLE_AUTO_MEMORY"] = "1"

    result = subprocess.run(
        args,
        input=prompt,
        encoding="utf-8",
        cwd=BRAIN_DIR,
        env=env,
        capture_output=True,
        text=True,
        timeout=300 if mode == "deep" else 120,
        check=False,
    )

    if result.returncode != 0:
        # Keep internal error details out of the device response.
        raise RuntimeError("Claude request failed")

    envelope = json.loads(result.stdout)
    if mode == "memory":
        _last["id"] = envelope.get("session_id")
        _last["time"] = time.time()

    response = envelope.get("structured_output")

    return validate_response(response, schema)

