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
gcc $CF -pthread apps/server/main.c core/match.c core/draft.c core/protocol.c core/card_rules.c core/card_text.c core/iduna.c core/http.c -o $B/dw_server_asan
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
echo "== e2e 4: draft mode -- separate queue, drafted decks, redraft/same-deck, deck log =="
rm -f "$W/matches.ndjson" "$W/decks.ndjson"
start_server --fast-forward
scripts/bot_pool.sh start --port "$PORT" --think-ms 0 --mode draft --pidfile "$W/dpool.pids" >/dev/null
sleep 1.5
# a random-queue human must NOT be paired with the draft bots: with no random bots it just waits (times out)
timeout 2 $B/dw_client --port "$PORT" --auto first-legal --name random-human --matches 1 --quiet > "$W/rh.out" && fail "random human was matched from a draft-only pool"
# a draft human drafts, plays, and requeues (same deck on a win/draw, redraft is exercised by bots losing)
timeout 20 $B/dw_client --port "$PORT" --mode draft --auto first-legal --name draft-human --matches 6 --quiet --seed 4 > "$W/dh.out" || fail "draft human did not finish 6 matches"
grep -q "matches=6" "$W/dh.out" || fail "draft human did not complete 6 matches: $(cat "$W/dh.out")"
for _ in $(seq 100); do n=$(wc -l < "$W/matches.ndjson" 2>/dev/null || echo 0); [ "$n" -ge 60 ] && break; sleep 0.1; done
scripts/bot_pool.sh stop --pidfile "$W/dpool.pids" >/dev/null
kill -TERM "$SPID"; wait "$SPID" || fail "draft server exited non-zero"; PIDS=()
grep -q "AddressSanitizer\|runtime error" "$W/server.err" && fail "sanitizer report (draft server)"
grep -q '"mode":"draft"' "$W/matches.ndjson" || fail "no draft matches in the match log"
[ -s "$W/decks.ndjson" ] || fail "no decks.ndjson written"
grep -q '"event":"draft"' "$W/decks.ndjson" && grep -q '"event":"match"' "$W/decks.ndjson" || fail "deck log lacks draft/match records"
nd=$(grep -c '"event":"draft"' "$W/decks.ndjson"); nm=$(grep -c '"event":"match"' "$W/decks.ndjson")
[ "$nd" -ge 4 ] || fail "expected >=4 drafted decks, got $nd"
[ "$nd" -lt "$nm" ] || fail "decks ($nd) should be fewer than deck-match records ($nm): winners keep their deck"
$B/replay_check "$W/matches.ndjson" 10 || fail "draft match log does not replay"
echo "OK: draft queue separate from random; $nd decks drafted, $nm deck-match records, log replays"

echo "== e2e 3: IDUNA auth + result reporting against a fake IDUNA (docs/IDUNA_CONTRACT.md) =="
command -v python3 >/dev/null || { echo "python3 missing, skipping e2e 3"; echo "E2E PASS (without IDUNA leg)"; exit 0; }
python3 tests/fake_iduna.py "$W/iduna.port" "$W/iduna.results" & IP=$!; PIDS+=("$IP")
for _ in $(seq 50); do [ -s "$W/iduna.port" ] && break; sleep 0.1; done
IPORT=$(cat "$W/iduna.port"); echo "sekret" > "$W/secret"
rm -f "$W/matches.ndjson"
start_server --fast-forward --iduna-url "http://127.0.0.1:$IPORT" --agent-secret-file "$W/secret"
grep -q "IDUNA auth required" "$W/server.out" || fail "server did not report auth required"
# no token / bad token are refused (server closes: client exits non-zero, never reaches a match)
timeout 15 $B/dw_client --port "$PORT" --auto random --name nobody --matches 1 --quiet --token "bad.token.x" && fail "bad token was admitted"
# bot authenticates with the agent secret; human with a (fake) guest token; they play; result reaches IDUNA
$B/dw_bot --archetype ripper --name bot-ripper --port "$PORT" --think-ms 0 --matches 3 --iduna-url "http://127.0.0.1:$IPORT" --agent-secret-file "$W/secret" > "$W/bot.out" 2>&1 & BP=$!; PIDS+=("$BP")
sleep 0.5
timeout 15 $B/dw_client --port "$PORT" --auto random --name Ada --matches 3 --quiet --token "guest.Ada.x" > "$W/ada.out" || fail "authenticated human failed to play"
grep -q "matches=3" "$W/ada.out" || fail "human did not complete 3 matches"
wait "$BP" || true
for _ in $(seq 30); do n=$(wc -l < "$W/iduna.results" 2>/dev/null || echo 0); [ "$n" -ge 3 ] && break; sleep 0.1; done
[ "$n" -ge 3 ] || fail "IDUNA received only $n match results (want 3)"
python3 - "$W/iduna.results" <<'PY' || fail "reported results malformed"
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1])]
assert all(r["winner"] in (0, 1, 2) and r["reason"] in ("hull", "rounds", "forfeit") and r["mode"] == 0 and r["seat0_player_id"] != r["seat1_player_id"] for r in rows), rows
PY
# IDUNA goes away: the server must keep serving already-admitted players, fail closed for new ones, and not crash
kill -TERM "$IP"; wait "$IP" 2>/dev/null || true
timeout 10 $B/dw_client --port "$PORT" --auto random --name late --matches 1 --quiet --token "guest.late.x" && fail "new player admitted while IDUNA down (fail-closed expected)"
kill -0 "$SPID" 2>/dev/null || fail "server died when IDUNA went away"
kill -TERM "$SPID"; wait "$SPID" || fail "IDUNA-leg server exited non-zero"; PIDS=()
grep -q "AddressSanitizer\|runtime error" "$W/server.err" && fail "sanitizer report (IDUNA server)"
echo "OK: IDUNA auth + reporting verified ($n results), fail-closed + no crash when IDUNA is down"
echo "E2E PASS"
