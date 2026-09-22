#!/usr/bin/env bash
# scripts/onboarding_screenshots.sh — headed, real, screenshot-verified walkthrough of the
# DEADWEIGHT onboarding funnel (kanban card #422, "automate the process of taking screenshots
# at each part of the deadweight onboarding"). Drives the REAL dw_gui binary (not --selftest's
# dummy video driver) under a real Xvfb X server via xdotool mouse clicks computed from the
# client's own logical layout (W=480 H=956, apps/gui/main.c), and captures each real screen with
# ImageMagick `import`. No mocked UI -- this is the same client a player runs, screenshotted.
#
# Usage: scripts/onboarding_screenshots.sh [--out DIR] [--iduna-url URL]
#   --out DIR         where PNGs land (default: docs/onboarding_screenshots)
#   --iduna-url URL   real IDUNA to guest-register against (default: http://127.0.0.1:8080 --
#                      the box's own local IDUNA; guest signup is rate-limited 3/day/IP, so
#                      re-runs reuse the same account file and don't re-register)
#
# Needs: Xvfb, ImageMagick (`import`), xdotool + libxdo3. If xdotool/libxdo3 aren't installed
# system-wide, this script extracts them from .deb via `apt-get download` + `dpkg-deb -x` into
# a cache dir under the working directory's build/ -- no root needed (same pattern already used
# elsewhere in this monorepo for mbedtls/xdotool sandbox setup).
set -euo pipefail
cd "$(dirname "$0")/.."
REPO_ROOT="$(pwd)"

OUT_DIR="docs/onboarding_screenshots"
IDUNA_URL="http://127.0.0.1:8080"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT_DIR="$2"; shift 2 ;;
    --iduna-url) IDUNA_URL="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 1 ;;
  esac
done
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

DEPS_DIR="$REPO_ROOT/build/onboarding_shot_deps"
mkdir -p "$DEPS_DIR"

# ---- tool discovery / no-root fallback ----
XDOTOOL_BIN="$(command -v xdotool || true)"
LIBXDO_DIR=""
if [[ -z "$XDOTOOL_BIN" ]]; then
  echo "== xdotool not on PATH -- extracting a local copy (no root) =="
  if [[ ! -x "$DEPS_DIR/xdotool/usr/bin/xdotool" ]]; then
    ( cd "$DEPS_DIR" && apt-get download xdotool libxdo3 2>&1 | tail -5 )
    mkdir -p "$DEPS_DIR/xdotool" "$DEPS_DIR/libxdo"
    dpkg-deb -x "$DEPS_DIR"/xdotool_*.deb "$DEPS_DIR/xdotool"
    dpkg-deb -x "$DEPS_DIR"/libxdo3_*.deb "$DEPS_DIR/libxdo"
  fi
  XDOTOOL_BIN="$DEPS_DIR/xdotool/usr/bin/xdotool"
  LIBXDO_DIR="$(dirname "$(find "$DEPS_DIR/libxdo" -name 'libxdo.so.3' | head -1)")"
fi
command -v Xvfb >/dev/null || { echo "Xvfb not found -- install it (apt-get install xvfb)"; exit 1; }
command -v import >/dev/null || { echo "ImageMagick 'import' not found -- install imagemagick"; exit 1; }

# ---- binaries ----
[[ -x build/dw_gui && -x build/dw_server && -x build/dw_bot ]] || {
  echo "== building dw_gui/dw_server/dw_bot =="
  bash scripts/build.sh --gui
}

# ---- throwaway X display + services (never touch production) ----
DISP=":$(( (RANDOM % 400) + 90 ))"
PORT=$(( (RANDOM % 5000) + 15000 ))
WORKDIR="$(mktemp -d)"
PIDS=()
cleanup() {
  for p in "${PIDS[@]:-}"; do kill "$p" >/dev/null 2>&1 || true; done
  rm -rf "$WORKDIR"
}
trap cleanup EXIT

echo "== starting Xvfb on $DISP =="
Xvfb "$DISP" -screen 0 1280x800x24 >"$WORKDIR/xvfb.log" 2>&1 &
PIDS+=("$!")
sleep 1

echo "== starting throwaway dw_server on :$PORT (no-auth, fast rounds, disposable match-log) =="
./build/dw_server --port "$PORT" --bind 127.0.0.1 --no-auth --round-ms 2000 \
  --match-log "$WORKDIR/matches" >"$WORKDIR/server.log" 2>&1 &
PIDS+=("$!")
sleep 1

run_xdotool() {
  if [[ -n "$LIBXDO_DIR" ]]; then
    LD_LIBRARY_PATH="$LIBXDO_DIR:${LD_LIBRARY_PATH:-}" DISPLAY="$DISP" "$XDOTOOL_BIN" "$@"
  else
    DISPLAY="$DISP" "$XDOTOOL_BIN" "$@"
  fi
}
shot() {
  local name="$1"
  local win; win="$(run_xdotool search --name "" | head -1)"
  DISPLAY="$DISP" import -window "$win" "$OUT_DIR/$name.png"
  echo "  captured $name.png"
}
# Logical canvas is W=480 H=956 (apps/gui/main.c); window is 1280x800, letterboxed left/right.
# click LX LY in logical coords -> real screen coords.
SCALE="0.836820083682"   # min(1280/480, 800/956)
OFFX="439.15"
click() {
  local lx="$1" ly="$2"
  local sx sy
  sx=$(python3 -c "print(int($OFFX + $lx*$SCALE))")
  sy=$(python3 -c "print(int($ly*$SCALE))")
  local win; win="$(run_xdotool search --name "" | head -1)"
  run_xdotool mousemove --window "$win" "$sx" "$sy" click 1
}

# Guest signup is rate-limited 3/day/IP server-side -- reuse ONE cached account across runs
# (persisted outside $WORKDIR, which is wiped on every exit) so repeat runs of this script don't
# burn through that quota re-registering a fresh guest every time.
ACCOUNT_FILE="$DEPS_DIR/onboarding_shot_account.txt"
echo "== launching real dw_gui (guest auth against $IDUNA_URL, account cached at $ACCOUNT_FILE) =="
DISPLAY="$DISP" ./build/dw_gui --host 127.0.0.1 --port "$PORT" --iduna-url "$IDUNA_URL" \
  --account "$ACCOUNT_FILE" >"$WORKDIR/gui.log" 2>&1 &
PIDS+=("$!")
sleep 2
grep -q "IDUNA auth ok" "$WORKDIR/gui.log" && echo "  $(grep 'IDUNA auth ok' "$WORKDIR/gui.log")"

echo "== 1/5: menu (guest state) =="
shot "01_menu_guest"

echo "== 2/5: claim account modal =="
click 240 526   # SECURE CONNECTION button center
sleep 1
shot "02_claim_account_modal"
run_xdotool key --clearmodifiers Escape   # X has no WM here (PointerRoot focus) -- a plain
sleep 1                                   # key event (not --window's XSendEvent) is what SDL sees

echo "== 3/5: matchmaking queue =="
click 240 368   # PRACTICE button center (rect 40,338,400,60 -- apps/gui/main.c draw_menu)
sleep 1
shot "03_queue_searching"

echo "== starting a bot opponent so the queue actually matches =="
./build/dw_bot --archetype wall --host 127.0.0.1 --port "$PORT" --matches 3 --think-ms 200 \
  >"$WORKDIR/bot.log" 2>&1 &
PIDS+=("$!")
sleep 2

echo "== 4/5: live match (card picks) =="
shot "04_match_card_pick"

echo "== 5/5: match end screen =="
# dw_gui prints no stdout signal for match end (only --selftest does) -- with a 2s round timer
# and no player input the bot wins on auto-pass timeouts well within 30s, live-verified.
sleep 30
shot "05_match_end"

echo
echo "Done. Screenshots in $OUT_DIR:"
ls -la "$OUT_DIR"
