#!/usr/bin/env python3
"""
Clawd Mochi — Claude Code hook bridge.

Reads a Claude Code hook event JSON from stdin and either:

1.  Posts a corresponding /event to the ESP32 (most events), or
2.  For PreToolUse on "dangerous" tools (Bash/Edit/Write/...), blocks while
    polling Mochi's touch sensor. Returns a `permissionDecision` JSON to
    Claude Code via stdout so a head-pat actually approves the tool call.

Multi-session arbitration: whichever session most recently submitted a
prompt becomes the "primary" — only its events are forwarded to Mochi.
Concurrent sessions in other terminals are silently ignored, so Mochi
mirrors the one you're currently typing into.

Failures are silent. Mochi never blocks Claude Code on bridge errors.
"""

import json
import os
import sys
import time
import urllib.parse
import urllib.request

HERE       = os.path.dirname(os.path.abspath(__file__))
CONFIG     = os.path.join(HERE, "config.json")
STATE_DIR  = os.path.expanduser("~/.cache/clawd-mochi")
STATE_FILE = os.path.join(STATE_DIR, "state.json")

HTTP_TIMEOUT_S = 1.5
DANGEROUS_TOOLS = {"Bash", "Edit", "Write", "MultiEdit", "NotebookEdit"}


# ── Config ────────────────────────────────────────────────────────

def load_config() -> dict:
    try:
        with open(CONFIG, "r") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


# ── ESP32 HTTP ────────────────────────────────────────────────────

def post_event(ip: str, event_type: str, meta: str = "") -> None:
    q = urllib.parse.urlencode({"type": event_type, "meta": meta[:60]})
    try:
        req = urllib.request.Request(f"http://{ip}/event?{q}", method="POST")
        urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_S).read()
    except Exception:
        pass


def get_state(ip: str) -> dict:
    try:
        with urllib.request.urlopen(f"http://{ip}/state", timeout=HTTP_TIMEOUT_S) as r:
            return json.loads(r.read())
    except Exception:
        return {}


def wait_for_touch(ip: str, timeout_s: float):
    """Block until a new touch / gesture event arrives. Returns 'short' |
    'long' | 'double' | None."""
    baseline = get_state(ip).get("touchTs", 0)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        time.sleep(0.15)
        st = get_state(ip)
        ts = st.get("touchTs", 0)
        if ts > baseline:
            return st.get("touch", "")
    return None


# ── Session arbitration ───────────────────────────────────────────

def _read_state() -> dict:
    try:
        with open(STATE_FILE, "r") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def _write_state(payload: dict) -> None:
    os.makedirs(STATE_DIR, exist_ok=True)
    tmp = STATE_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(payload, f)
    os.replace(tmp, STATE_FILE)   # atomic on POSIX


def claim_primary(session_id: str) -> None:
    if not session_id:
        return
    state = _read_state()
    state["primary_session"] = session_id
    state["last_prompt_ts"]  = time.time()
    _write_state(state)


def is_primary(session_id: str) -> bool:
    """No primary recorded yet → treat as primary (cold start). Otherwise
    only forward events from the recorded primary session."""
    if not session_id:
        return True
    primary = _read_state().get("primary_session", "")
    return (not primary) or session_id == primary


# ── Permission loop ───────────────────────────────────────────────

def describe_tool(data: dict) -> str:
    name = data.get("tool_name", "")
    inp  = data.get("tool_input", {}) or {}
    if name == "Bash":
        return f"Bash: {inp.get('command', '')[:30]}"
    if name in ("Edit", "Write", "MultiEdit"):
        return f"{name}: {os.path.basename(inp.get('file_path', ''))}"
    if name == "NotebookEdit":
        return f"Notebook: {os.path.basename(inp.get('notebook_path', ''))}"
    return name


def emit_decision(decision: str, reason: str) -> None:
    """Print the hook decision JSON to stdout — Claude Code reads this to
    bypass the normal terminal y/n prompt."""
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": decision,
            "permissionDecisionReason": reason,
        }
    }))


def handle_pretooluse(data: dict, ip: str, cfg: dict) -> None:
    tool = data.get("tool_name", "")

    if not cfg.get("permission_loop_enabled", True):
        post_event(ip, "tool_pre", tool)
        return

    if tool not in DANGEROUS_TOOLS:
        post_event(ip, "tool_pre", tool)
        return

    # Dangerous tool → ask via Mochi
    desc    = describe_tool(data)
    timeout = float(cfg.get("permission_timeout_s", 15.0))

    post_event(ip, "permission", desc)
    event = wait_for_touch(ip, timeout)

    if event == "short":
        post_event(ip, "tool_pre", tool)
        emit_decision("allow", "Approved via Mochi touch / tap")
    elif event == "long":
        post_event(ip, "idle")
        emit_decision("deny", "Denied via Mochi long-press")
    else:
        # double-tap or timeout → fall through to Claude's normal y/n
        post_event(ip, "idle")
        # (no stdout JSON = no decision = Claude prompts in terminal)


# ── Main ──────────────────────────────────────────────────────────

def main() -> int:
    try:
        data = json.load(sys.stdin)
    except Exception:
        return 0

    cfg = load_config()
    ip  = cfg.get("esp32_ip", "")
    if not ip:
        return 0

    sid   = data.get("session_id", "")
    event = data.get("hook_event_name", "")

    # UserPromptSubmit always claims primary, then forwards.
    if event == "UserPromptSubmit":
        claim_primary(sid)
        post_event(ip, "prompt")
        return 0

    # Every other event filters by primary.
    if not is_primary(sid):
        return 0

    if event == "PreToolUse":
        handle_pretooluse(data, ip, cfg)

    elif event == "PostToolUse":
        resp = data.get("tool_response")
        err  = None
        if isinstance(resp, dict):
            err = resp.get("error") or resp.get("stderr")
        if err:
            post_event(ip, "error", str(err))
        else:
            post_event(ip, "tool_post", data.get("tool_name", ""))

    elif event == "Notification":
        post_event(ip, "permission", data.get("message", "attention"))

    elif event in ("Stop", "SubagentStop"):
        post_event(ip, "stop")

    elif event == "SessionStart":
        post_event(ip, "idle")

    return 0


if __name__ == "__main__":
    sys.exit(main())
