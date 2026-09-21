#!/usr/bin/env bash
# Deploy dw_server + the 3-bot pool as USER systemd units on this box (no sudo). Idempotent.
# Binaries are COPIED to ~/.local/opt/deadweight/bin because scripts/build.sh wipes build/ on every run
# (a service pointing at build/ would break the next time anyone builds).
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/build.sh
BIN="$HOME/.local/opt/deadweight/bin"; CONF="$HOME/.config/deadweight"; UNITS="$HOME/.config/systemd/user"
# S521: match logs live outside the git checkout now (see dw.env.example) -- the service's own
# ExecStartPre already mkdir -p's DW_MATCH_LOG_DIR, no need to pre-create a repo-relative path here.
mkdir -p "$BIN" "$CONF" "$UNITS"
install -m 0755 build/dw_server build/dw_bot "$BIN/"
if [ ! -f "$CONF/dw.env" ]; then sed "s|/home/USER|$HOME|" ops/systemd/dw.env.example > "$CONF/dw.env"; fi
cp ops/systemd/dw-server.service 'ops/systemd/dw-bot@.service' 'ops/systemd/dw-draft-bot@.service' "$UNITS/"
systemctl --user daemon-reload
systemctl --user enable dw-server; systemctl --user restart dw-server
for a in ripper wall mirror; do systemctl --user enable "dw-bot@$a"; systemctl --user restart "dw-bot@$a"; systemctl --user enable "dw-draft-bot@$a"; systemctl --user restart "dw-draft-bot@$a"; done   # random pool + draft pool
sleep 1
systemctl --user --no-pager --failed || true
ss -ltn | grep ":$(grep ^DW_PORT "$CONF/dw.env" | cut -d= -f2 | cut -d' ' -f1) " || echo "WARNING: dw_server not listening"
