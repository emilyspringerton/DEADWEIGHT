#!/usr/bin/env bash
# Headless GUI smoke: real dw_server --fast-forward --no-auth + one real dw_bot + `dw_gui --selftest` (SDL dummy video
# driver, software renderer) plays a full match through the GUI's own state machine and dumps two frames as BMP.
# Kills ONLY the exact PIDs it started. Usage: tests/test_gui_selftest.sh [frames-dir]
set -euo pipefail
cd "$(dirname "$0")/.."
B=build
for t in dw_server dw_bot dw_gui; do [ -x $B/$t ] || { echo "build $B/$t first (scripts/build.sh --gui)"; exit 1; }; done
W=$(mktemp -d); PIDS=()
FRAMES="${1:-$W}"; mkdir -p "$FRAMES"
cleanup() { for p in "${PIDS[@]:-}"; do [ -n "$p" ] && kill -TERM "$p" 2>/dev/null || true; done; rm -rf "$W"; }
trap cleanup EXIT
$B/dw_server --port 0 --fast-forward --no-auth > "$W/server.out" 2> "$W/server.err" & PIDS+=("$!")
PORT=""
for _ in $(seq 50); do PORT=$(grep -o 'listening on [0-9.]*:[0-9]*' "$W/server.out" | head -1 | sed 's/.*://') || true; [ -n "$PORT" ] && break; sleep 0.1; done
[ -n "$PORT" ] || { echo "GUI SELFTEST FAIL: server did not start"; cat "$W/server.err"; exit 1; }
$B/dw_bot --archetype ripper --name bot-ripper --port "$PORT" --think-ms 0 > "$W/bot.out" 2>&1 & PIDS+=("$!")
sleep 0.5
if ! SDL_VIDEODRIVER=dummy timeout 40 $B/dw_gui --selftest --name gui-test --host 127.0.0.1 --port "$PORT" --frames "$FRAMES" > "$W/gui.out" 2> "$W/gui.err"; then
  echo "GUI SELFTEST FAIL"; cat "$W/gui.out" "$W/gui.err"; exit 1
fi
cat "$W/gui.out"
grep -q "full match played" "$W/gui.out" || { echo "GUI SELFTEST FAIL: no completed match"; exit 1; }
[ -s "$FRAMES/dw_gui_match.bmp" ] && [ -s "$FRAMES/dw_gui_end.bmp" ] || { echo "GUI SELFTEST FAIL: frames missing"; exit 1; }
echo "GUI SELFTEST OK (random)"
# draft mode: the GUI drafts 16 picks through its own draft screen, then plays a match with the drafted deck
$B/dw_bot --archetype wall --mode draft --name bot-wall-d --port "$PORT" --think-ms 0 > "$W/dbot.out" 2>&1 & PIDS+=("$!")
sleep 0.5
mkdir -p "$FRAMES/draft"
if ! SDL_VIDEODRIVER=dummy timeout 40 $B/dw_gui --selftest --mode draft --name gui-draft --host 127.0.0.1 --port "$PORT" --frames "$FRAMES/draft" > "$W/gui2.out" 2> "$W/gui2.err"; then
  echo "GUI DRAFT SELFTEST FAIL"; cat "$W/gui2.out" "$W/gui2.err"; exit 1
fi
cat "$W/gui2.out"
grep -q "full match played" "$W/gui2.out" || { echo "GUI DRAFT SELFTEST FAIL: no completed match"; exit 1; }
[ -s "$FRAMES/draft/dw_gui_draft.bmp" ] && [ -s "$FRAMES/draft/dw_gui_match.bmp" ] && [ -s "$FRAMES/draft/dw_gui_end.bmp" ] || { echo "GUI DRAFT SELFTEST FAIL: frames missing"; exit 1; }
echo "GUI DRAFT SELFTEST OK"
