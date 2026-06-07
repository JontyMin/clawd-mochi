# Clawd Mochi — Claude Code bridge

A tiny Python hook that mirrors your Claude Code session onto the physical Clawd Mochi sitting on your desk.

```
Claude Code  ──hook JSON──▶  claude_hook.py  ──HTTP POST──▶  Mochi (ESP32-C3 on home WiFi)
```

No daemon, no dependencies beyond Python 3 (already on macOS). Every hook fires the script, which POSTs one event and exits.

## Install

1. Flash the Clawd Mochi firmware to your ESP32 and complete first-time WiFi setup so the display shows an IP address (e.g. `192.168.1.42`).
2. Run:
   ```bash
   bash bridge/install.sh
   ```
3. Enter the IP shown on the display when prompted.
4. Start a new Claude Code session — Mochi should react.

The installer is idempotent: re-running just updates the IP and reinstalls hooks.

## What gets installed

`install.sh` adds these hooks to `~/.claude/settings.json` (merging with any existing hooks, not overwriting):

| Hook event | What Mochi does |
|------------|-----------------|
| `UserPromptSubmit` | Switch to **thinking** — squish eyes pulse |
| `PreToolUse` | Switch to **working** — display the tool name |
| `PostToolUse` | Back to **thinking** — or **error** if the tool failed |
| `Notification` | **Permission** — flashing frame + buzzer alert; tap Mochi to confirm |
| `Stop` / `SubagentStop` | **Done** — satisfied eyes + chime, returns to idle after 3s |
| `SessionStart` | Back to **idle** |

## Test it without Claude Code

The ESP32 accepts plain `curl` for debugging:

```bash
curl 'http://<mochi-ip>/event?type=prompt'
curl 'http://<mochi-ip>/event?type=tool_pre&meta=Bash'
curl 'http://<mochi-ip>/event?type=permission&meta=Write%20to%20foo.py'
curl 'http://<mochi-ip>/event?type=stop'
curl 'http://<mochi-ip>/event?type=error&meta=oops'
curl 'http://<mochi-ip>/event?type=idle'
```

## Touch interaction — closes the loop

For "dangerous" tools (`Bash` / `Edit` / `Write` / `MultiEdit` / `NotebookEdit`) the `PreToolUse` hook blocks Claude Code while it polls Mochi:

- **TTP223 short tap** (head pat) or **MPU6050 tap** (desk thump) → bridge returns `{"permissionDecision": "allow"}` → Claude proceeds without prompting in the terminal
- **TTP223 long press** (>800ms) → `permissionDecision: "deny"` → Claude aborts the tool call
- **Double tap or 15s timeout** → bridge returns no decision → Claude falls back to the normal terminal y/n prompt

Other tools (`Read`, `Glob`, `Grep`, ...) are not gated — they pass through silently.

Tunables in `config.json`:

```json
{
  "esp32_ip": "192.168.1.42",
  "permission_loop_enabled": true,
  "permission_timeout_s": 15
}
```

Set `permission_loop_enabled` to `false` to disable the gating entirely (Mochi still gets the `permission` event for the visual + buzzer; Claude prompts as usual).

Other touch behaviours outside permission mode:

- **TTP223 long press** → toggles buzzer mute
- **MPU6050 shake** → forces Mochi back to idle
- **Double tap** → forces Mochi back to idle

## Multi-session arbitration

When two Claude Code terminals are running at once, only the **most recently prompted** session is mirrored to Mochi. State lives in `~/.cache/clawd-mochi/state.json` (atomic-write, no daemon required):

```json
{ "primary_session": "<uuid>", "last_prompt_ts": 1717000000 }
```

Every `UserPromptSubmit` claims primary. All other hook events check the recorded primary and silently skip if they don't match. Switching terminals and hitting enter is enough to "take over" Mochi.

If you want both sessions visible at once: not in this release — you'd need per-window focus tracking via macOS Accessibility APIs and a persistent daemon. Out of scope for the zero-dependency bridge.

## Files

| File | Purpose |
|------|---------|
| `claude_hook.py` | Reads hook JSON from stdin → forwards to Mochi; blocks on `PreToolUse` for dangerous tools to wait for a head-pat / desk-tap decision |
| `install.sh` | Asks for IP, writes/updates `config.json` (preserves your tuned settings), merges hooks into `settings.json` |
| `uninstall.sh` | Removes Clawd Mochi hooks (leaves other hooks intact) |
| `config.json` | Created by installer; `esp32_ip`, `permission_loop_enabled`, `permission_timeout_s`. Gitignored. |
| `~/.cache/clawd-mochi/state.json` | Runtime state — current primary session id. Safe to delete; will be re-created on next prompt. |

## Troubleshooting

- **Mochi shows "Online!" but does not react** — make sure your Mac is on the same WiFi network. Run a `curl` test (above) directly to confirm reachability.
- **`config.json` missing** — re-run `bash bridge/install.sh`.
- **Want to switch to a different ESP32 / WiFi** — on the device's web UI, POST `/factoryreset` (or just change WiFi networks — Mochi will fall back to AP mode and re-provision).
- **Want to disable Mochi mirroring** — run `bash bridge/uninstall.sh`.
