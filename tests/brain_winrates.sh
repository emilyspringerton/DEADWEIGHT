#!/usr/bin/env bash
# Hybrid-brain win-rate table (S503-14b): the measured side is a dw_bot (kind=bot) using --brain hybrid with the given
# weights; opponents are the plain heuristic archetypes / random driven by dw_client (kind=human, so the server pairs
# them ahead of the bot-reservation rule). Throwaway dw_server on a free port, --fast-forward --no-auth, exact-PID kills.
# Usage: tests/brain_winrates.sh [MATCHES=400] [extra dw_bot args...]   e.g. tests/brain_winrates.sh 400 --wh 1 --wn 0 --sigma 0
set -uo pipefail
cd "$(dirname "$0")/.."
N="${1:-400}"; shift || true
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
./build/dw_server --port "$PORT" --fast-forward --no-auth >/dev/null 2>&1 & SPID=$!
trap 'kill $SPID 2>/dev/null; wait $SPID 2>/dev/null' EXIT
sleep 0.5
printf "%-14s | %-8s %-8s %-8s %-8s\n" "hybrid \\ opp" ripper wall mirror random
for H in ripper wall mirror; do
  row=""
  for O in ripper wall mirror random; do
    ./build/dw_client --host 127.0.0.1 --port "$PORT" --name "opp-$O" --auto "$O" --matches $((N * 3)) --quiet >/dev/null 2>&1 & CPID=$!
    line=$(./build/dw_bot --archetype "$H" --name "hy-$H" --host 127.0.0.1 --port "$PORT" --matches "$N" "$@" 2>&1 | grep "matches=")
    kill $CPID 2>/dev/null; wait $CPID 2>/dev/null
    w=$(sed -n 's/.*wins=\([0-9]*\).*/\1/p' <<<"$line"); l=$(sed -n 's/.*losses=\([0-9]*\).*/\1/p' <<<"$line"); d=$(sed -n 's/.*draws=\([0-9]*\).*/\1/p' <<<"$line")
    row+=$(printf " %5.1f%%   " "$(python3 -c "print(100.0*($w+0.5*$d)/max(1,$w+$l+$d))")")
  done
  printf "%-14s |%s\n" "$H" "$row"
done
echo "(score = wins + 0.5*draws over $N matches per cell; >50% = the hybrid bot is ahead; args: $*)"
