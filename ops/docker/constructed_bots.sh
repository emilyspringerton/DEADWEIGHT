#!/bin/bash
# constructed_bots.sh [dw_bot args...] -- the standing CONSTRUCTED-queue bots, all in ONE container.
# One dw_bot per deck tier: tier 1 = the weakest slice of IDUNA's draft-deck tracker, tier $DW_TIERS (default 5) = the strongest. The server
# pairs a human with the waiting bot whose ladder rating is nearest theirs, so the tiers sort themselves by what their decks earn. The
# archetype (play style) cycles mirror, wall, ripper, mirror, wall over the tiers; names are bot-<archetype>-c<tier>.
# One container, not one per bot, because cluster memory requests are ~99% (a dw_bot is a few MB).
# This container must NEVER exit on its own: it shares a pod with dw_server, and a container that crash-loops makes the pod NotReady and the
# game's Service lose its endpoints (it happened with the logserve sidecar). So a bot that exits (IDUNA unreachable at start, a bad flag) is
# restarted by itself with a growing pause (2 s .. 60 s; reset once a bot has run for a minute) while the others keep playing.
# SIGTERM (kubelet) stops everything and exits 143. Every argument is passed on to each dw_bot, e.g. --host 127.0.0.1 --port 7180
# --think-ms 300 --iduna-url ... --agent-secret-file ...
set -u
TIERS="${DW_TIERS:-5}"
BOT="${DW_BOT:-/app/dw_bot}"
archs=(mirror wall ripper mirror wall)

run_tier() {   # run_tier TIER ARCHETYPE ARGS... : keeps that tier's bot alive until SIGTERM
  local t="$1" a="$2" child="" delay=0 started
  shift 2
  trap '[ -n "$child" ] && kill "$child" 2>/dev/null; exit 0' TERM INT
  while :; do
    started=$SECONDS
    "$BOT" --archetype "$a" --name "bot-$a-c$t" --mode constructed --tier "$t" --tiers "$TIERS" "$@" &
    child=$!
    wait "$child"; local rc=$?
    child=""
    if [ $((SECONDS - started)) -ge 60 ]; then delay=2; elif [ "$delay" -eq 0 ]; then delay=2; else delay=$(( delay >= 30 ? 60 : delay * 2 )); fi
    echo "constructed_bots: tier $t bot (bot-$a-c$t) exited ($rc); restarting in ${delay}s" >&2
    sleep "$delay" & child=$!
    wait "$child"; child=""
  done
}

pids=()
trap 'kill "${pids[@]}" 2>/dev/null; wait 2>/dev/null; exit 143' TERM INT
for t in $(seq 1 "$TIERS"); do
  run_tier "$t" "${archs[$(( (t - 1) % ${#archs[@]} ))]}" "$@" &
  pids+=($!)
done
echo "constructed_bots: $TIERS bots started (tiers 1..$TIERS)" >&2
wait
