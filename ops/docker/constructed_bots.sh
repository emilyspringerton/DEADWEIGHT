#!/bin/bash
# constructed_bots.sh [dw_bot args...] -- the standing CONSTRUCTED-queue bots, all in ONE container.
# One dw_bot per deck tier: tier 1 = the weakest slice of IDUNA's draft-deck tracker, tier $DW_TIERS (default 5) = the strongest. The server
# pairs a human with the waiting bot whose ladder rating is nearest theirs, so the tiers sort themselves by what their decks earn. The
# archetype (play style) cycles mirror, wall, ripper, mirror, wall over the tiers; names are bot-<archetype>-c<tier>.
# One container, not one per bot, because cluster memory requests are ~99% (a dw_bot is a few MB). The bots reconnect on their own
# (dw_bot's own backoff loop); if one EXITS (cannot reach IDUNA, bad flags) the rest are stopped too and the container restarts as a whole.
# Every argument is passed on to each dw_bot, e.g. --host 127.0.0.1 --port 7180 --think-ms 300 --iduna-url ... --agent-secret-file ...
set -u
TIERS="${DW_TIERS:-5}"
BOT="${DW_BOT:-/app/dw_bot}"
archs=(mirror wall ripper mirror wall)
pids=()
stop() { kill "${pids[@]}" 2>/dev/null; wait 2>/dev/null; }
trap 'stop; exit 143' TERM INT
for t in $(seq 1 "$TIERS"); do
  a="${archs[$(( (t - 1) % ${#archs[@]} ))]}"
  "$BOT" --archetype "$a" --name "bot-$a-c$t" --mode constructed --tier "$t" --tiers "$TIERS" "$@" &
  pids+=($!)
done
echo "constructed_bots: $TIERS bots started (tiers 1..$TIERS)" >&2
wait -n
echo "constructed_bots: a bot exited; stopping the others so the container restarts" >&2
stop
exit 1
