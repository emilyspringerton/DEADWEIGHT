#!/usr/bin/env bash
# Start / stop the standing 3-bot pool (ripper, wall, mirror) against a dw_server.
#   scripts/bot_pool.sh start [--host H] [--port P] [--think-ms N] [--bin PATH] [--pidfile F] [--mode random|draft]
#   scripts/bot_pool.sh stop  [--pidfile F]
# The bots are hand-written HEURISTIC archetypes, not learned policies. `stop` kills ONLY the exact PIDs recorded
# in the pidfile -- never a broad pkill (a past incident took down a live shared server that way).
set -euo pipefail
cd "$(dirname "$0")/.."
cmd="${1:-}"; shift || true
HOST=127.0.0.1; PORT=7700; THINK=300; BIN=build/dw_bot; PIDFILE=build/bot_pool.pids; MODE=random
while [ $# -gt 0 ]; do
  case "$1" in
    --host) HOST="$2"; shift 2;; --port) PORT="$2"; shift 2;; --think-ms) THINK="$2"; shift 2;;
    --bin) BIN="$2"; shift 2;; --pidfile) PIDFILE="$2"; shift 2;; --mode) MODE="$2"; shift 2;; *) echo "unknown arg $1" >&2; exit 2;;
  esac
done
case "$cmd" in
  start)
    if [ -f "$PIDFILE" ]; then
      while read -r pid; do kill -0 "$pid" 2>/dev/null && { echo "pool already running (pid $pid); stop it first" >&2; exit 1; }; done < "$PIDFILE"
      rm -f "$PIDFILE"
    fi
    SUF=""; [ "$MODE" = draft ] && SUF="-d"
    mkdir -p "$(dirname "$PIDFILE")"; : > "$PIDFILE"
    for arch in ripper wall mirror; do
      "$BIN" --archetype "$arch" --name "bot-$arch${SUF}" --mode "$MODE" --host "$HOST" --port "$PORT" --think-ms "$THINK" --seed "$RANDOM" &
      echo $! >> "$PIDFILE"
    done
    echo "started 3 $MODE-queue bots (ripper wall mirror) -> $HOST:$PORT, pids in $PIDFILE";;
  stop)
    [ -f "$PIDFILE" ] || { echo "no pidfile $PIDFILE"; exit 0; }
    while read -r pid; do
      [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null && kill -TERM "$pid" || true
    done < "$PIDFILE"
    rm -f "$PIDFILE"; echo "stopped";;
  *) echo "usage: $0 start|stop [opts]" >&2; exit 2;;
esac
