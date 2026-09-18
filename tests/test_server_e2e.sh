#!/usr/bin/env bash
# End-to-end: sanitizer-built dw_server (--fast-forward) + the real 3-bot pool + real dw_client processes.
# Asserts: human-vs-bot match completes; bots also fight each other; >=200 matches survived with no ASan/UBSan
# report; the last-waiting-bot reservation holds (late human gets a bot within 1s); garbage on the wire doesn't
# kill the server; the round timer (--round-ms) auto-passes a slow player; match log replays faithfully.
# Kills ONLY the exact PIDs it started.
set -euo pipefail
cd "$(dirname "$0")/.."
B=build; mkdir -p $B
CF="-std=c99 -Wall -Wextra -Werror -Icore -Icore/runtime -DPARENA_NO_GRAPHICS -g -fsanitize=address,undefined -fno-sanitize-recover=all"
gcc $CF apps/server/main.c core/match.c core/protocol.c core/card_rules.c -o $B/dw_server_asan
gcc $CF tests/replay_check.c core/match.c core/card_rules.c -o $B/replay_check
for t in dw_bot dw_client; do [ -x $B/$t ] || { echo "build $B/$t first (scripts/build.sh)"; exit 1; }; done

W=$(mktemp -d); PIDS=()
cleanup() { for p in "${PIDS[@]:-}"; do [ -n "$p" ] && kill -TERM "$p" 2>/dev/null || true; done; [ -f "$W/pool.pids" ] && scripts/bot_pool.sh stop --pidfile "$W/pool.pids" >/dev/null 2>&1 || true; rm -rf "$W"; }
trap cleanup EXIT
fail() { echo "E2E FAIL: $*"; echo "--- server stderr ---"; tail -20 "$W/server.err" 2>/dev/null || true; exit 1; }

start_server() { # args...; sets PORT, SPID
  : > "$W/server.out"; : > "$W/server.err"
  $B/dw_server_asan --port 0 --match-log "$W" "$@" > "$W/server.out" 2> "$W/server.err" & SPID=$!; PIDS+=("$SPID")
  for _ in $(seq 50); do PORT=$(grep -o 'listening on [0-9.]*:[0-9]*' "$W/server.out" | head -1 | sed 's/.*://') || true; [ -n "${PORT:-}" ] && return 0; sleep 0.1; done
  fail "server did not start"
}

echo "== e2e 1: fast-forward server + 3-bot pool + humans =="
start_server --fast-forward
scripts/bot_pool.sh start --port "$PORT" --think-ms 0 --pidfile "$W/pool.pids" >/dev/null
sleep 1.5   # bots settle: two fight, one is reserved/waiting

# garbage on the wire must not take the server down
( exec 3<>/dev/tcp/127.0.0.1/"$PORT"; printf '\xff\xff\xff\xff garbage' >&3; sleep 0.2 ) 2>/dev/null || true
( exec 3<>/dev/tcp/127.0.0.1/"$PORT"; printf '\x02\x00\x02\x00' >&3; sleep 0.2 ) 2>/dev/null || true   # QUEUE before HELLO

# late-joining human is matched to a bot within 1s
t0=$(date +%s%N)
timeout 2 $B/dw_client --port "$PORT" --auto random --name late-human --matches 1 --seed 3 > "$W/human1.out" || fail "late human did not finish a match within 2s"
t1=$(date +%s%N); ms=$(( (t1 - t0) / 1000000 ))
grep -q "(bot)" "$W/human1.out" || fail "late human's opponent was not a bot: $(cat "$W/human1.out")"
[ "$ms" -lt 1000 ] || fail "late human waited ${ms}ms for a match (>1000ms)"
echo "late human matched to a bot and finished in ${ms}ms"

# a few more human-vs-bot matches, different policies
for pol in first-legal random ripper; do
  timeout 10 $B/dw_client --port "$PORT" --auto "$pol" --name "h-$pol" --matches 5 --quiet > "$W/h.out" || fail "human ($pol) failed"
  grep -q "matches=5" "$W/h.out" || fail "human ($pol) did not complete 5 matches"
done

# survive >= 200 matches (bots keep fighting)
for _ in $(seq 300); do n=$(wc -l < "$W/matches.ndjson" 2>/dev/null || echo 0); [ "$n" -ge 200 ] && break; sleep 0.1; done
[ "$n" -ge 200 ] || fail "only $n matches logged in 30s"
kill -0 "$SPID" 2>/dev/null || fail "server died"
scripts/bot_pool.sh stop --pidfile "$W/pool.pids" >/dev/null
kill -TERM "$SPID"; wait "$SPID" || fail "server exited non-zero"; PIDS=()
grep -q "AddressSanitizer\|runtime error" "$W/server.err" && fail "sanitizer report in server stderr"
summary=$(grep shutdown "$W/server.err"); echo "$summary"
bb=$(sed -n 's/.*bot_bot=\([0-9]*\).*/\1/p' <<<"$summary"); hb=$(sed -n 's/.*human_bot=\([0-9]*\).*/\1/p' <<<"$summary")
[ "${bb:-0}" -ge 1 ] || fail "no bot-vs-bot matches"; [ "${hb:-0}" -ge 16 ] || fail "expected >=16 human-vs-bot matches, got ${hb:-0}"
$B/replay_check "$W/matches.ndjson" 100 || fail "match log does not replay"
echo "OK: $n matches survived (bot_bot=$bb human_bot=$hb)"

echo "== e2e 2: round timer auto-passes a slow player =="
rm -f "$W/matches.ndjson"
start_server --round-ms 300
timeout 20 $B/dw_client --port "$PORT" --auto first-legal --name fast --matches 1 --quiet --seed 1 > "$W/fast.out" & FP=$!; PIDS+=("$FP")
timeout 20 $B/dw_client --port "$PORT" --auto first-legal --name slow --matches 1 --quiet --think-ms 700 --seed 2 > "$W/slow.out" & SP=$!; PIDS+=("$SP")
wait "$FP" || fail "fast client failed"; wait "$SP" || fail "slow client failed"
grep -q "matches=1" "$W/fast.out" && grep -q "matches=1" "$W/slow.out" || fail "timer match did not complete for both"
kill -TERM "$SPID"; wait "$SPID" || fail "timer server exited non-zero"; PIDS=()
grep -q "AddressSanitizer\|runtime error" "$W/server.err" && fail "sanitizer report (timer server)"
$B/replay_check "$W/matches.ndjson" 1 || fail "timer match log does not replay"
echo "OK: timeout auto-pass completed the match"
echo "E2E PASS"
