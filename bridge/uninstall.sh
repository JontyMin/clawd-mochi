#!/usr/bin/env bash
#
# Remove Clawd Mochi hooks from ~/.claude/settings.json.
# Leaves other hooks intact.
#
set -e

SETTINGS="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/settings.json"

if [ ! -f "$SETTINGS" ]; then
  echo "No settings.json at $SETTINGS — nothing to do."
  exit 0
fi

python3 - "$SETTINGS" <<'PY'
import json, sys

path = sys.argv[1]
with open(path) as f:
    s = json.load(f)

hooks = s.get('hooks', {})
changed = False
for ev, bucket in list(hooks.items()):
    new_bucket = []
    for group in bucket:
        sub = [h for h in group.get('hooks', [])
               if 'claude_hook.py' not in h.get('command', '')]
        if sub:
            new_bucket.append({**group, 'hooks': sub})
        else:
            changed = True
    if new_bucket != bucket:
        changed = True
    if new_bucket:
        hooks[ev] = new_bucket
    else:
        hooks.pop(ev, None)
        changed = True

if changed:
    with open(path, 'w') as f:
        json.dump(s, f, indent=2)
    print(f'Removed Clawd Mochi hooks from {path}')
else:
    print('No Clawd Mochi hooks found.')
PY
