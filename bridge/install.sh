#!/usr/bin/env bash
#
# Clawd Mochi bridge installer.
# Asks for the Mochi's IP, writes config.json, merges Claude Code hooks
# into ~/.claude/settings.json without clobbering existing entries.
#
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
HOOK_SCRIPT="$SCRIPT_DIR/claude_hook.py"
SETTINGS="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/settings.json"

echo "Clawd Mochi bridge installer"
echo "============================"
echo

# Existing IP, if any
DEFAULT_IP=""
if [ -f "$SCRIPT_DIR/config.json" ]; then
  DEFAULT_IP=$(python3 -c "import json; print(json.load(open('$SCRIPT_DIR/config.json')).get('esp32_ip',''))" 2>/dev/null || echo "")
fi

if [ -n "$DEFAULT_IP" ]; then
  read -r -p "Mochi IP address [$DEFAULT_IP]: " IP
  IP=${IP:-$DEFAULT_IP}
else
  read -r -p "Mochi IP address (shown on the display after WiFi connects): " IP
fi

if [ -z "$IP" ]; then
  echo "Need an IP. Aborting."
  exit 1
fi

# Write config.json — keeps any user-tuned settings, just updates the IP.
python3 - "$SCRIPT_DIR" "$IP" <<'PY'
import json, os, sys
script_dir, ip = sys.argv[1], sys.argv[2]
path = os.path.join(script_dir, 'config.json')
try:
    with open(path) as f:
        existing = json.load(f)
except (FileNotFoundError, json.JSONDecodeError):
    existing = {}
existing['esp32_ip'] = ip
# Defaults for the permission-loop feature; edit config.json to tune.
existing.setdefault('permission_loop_enabled', True)
existing.setdefault('permission_timeout_s', 15)
with open(path, 'w') as f:
    json.dump(existing, f, indent=2)
print(f'Wrote {path}')
PY

chmod +x "$HOOK_SCRIPT"

# Merge into Claude Code settings
mkdir -p "$(dirname "$SETTINGS")"
python3 - "$SETTINGS" "$HOOK_SCRIPT" <<'PY'
import json, sys, os

settings_path, hook_script = sys.argv[1], sys.argv[2]

try:
    with open(settings_path) as f:
        s = json.load(f)
except (FileNotFoundError, json.JSONDecodeError):
    s = {}

hooks = s.setdefault('hooks', {})

cmd = f'python3 {hook_script}'
events = ['UserPromptSubmit', 'PreToolUse', 'PostToolUse',
          'Notification', 'Stop', 'SubagentStop', 'SessionStart']

for ev in events:
    bucket = hooks.get(ev, [])
    # Strip any prior Clawd Mochi entries so re-installs are idempotent
    cleaned = []
    for group in bucket:
        sub = [h for h in group.get('hooks', [])
               if 'claude_hook.py' not in h.get('command', '')]
        if sub:
            cleaned.append({**group, 'hooks': sub})
    cleaned.append({
        'matcher': '',
        'hooks': [{'type': 'command', 'command': cmd}]
    })
    hooks[ev] = cleaned

with open(settings_path, 'w') as f:
    json.dump(s, f, indent=2)
print(f'Merged hooks into {settings_path}')
PY

echo
echo "Done. Start a new Claude Code session to activate."
echo "Smoke-test: curl 'http://$IP/event?type=permission&meta=test'"
