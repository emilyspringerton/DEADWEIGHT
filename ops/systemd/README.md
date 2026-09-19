# DEADWEIGHT systemd units (files only)

Nothing here is installed, enabled or started by this repo. To deploy (a human/ops step):

```bash
mkdir -p ~/.config/deadweight ~/.config/systemd/user
cp ops/systemd/dw.env.example ~/.config/deadweight/dw.env        # edit paths; REQUIRED
cp ops/systemd/dw-server.service 'ops/systemd/dw-bot@.service' ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now dw-server
for a in ripper wall mirror; do systemctl --user enable --now dw-bot@$a; done   # the pool of 3 (random queue)
# draft queue: its own pool of 3 (random and draft are separate queues)
cp 'ops/systemd/dw-draft-bot@.service' ~/.config/systemd/user/ && systemctl --user daemon-reload
for a in ripper wall mirror; do systemctl --user enable --now dw-draft-bot@$a; done
```

**The EnvironmentFile is required, on purpose.** `ecowar-matchmaker.service` sat dead for 5 days behind a missing
`EnvironmentFile`. Here a missing/unreadable `dw.env` makes the unit fail visibly (`systemctl --user --failed`);
`ExecStartPre` also fails loudly if the binary isn't built. Check with `systemctl --user status dw-server` and
`journalctl --user -u dw-server`. Match log (ndjson, replayable with `build/replay_check`) goes to `DW_MATCH_LOG_DIR`.

Bots are hand-written heuristic archetypes (ripper / wall / mirror), not learned policies. The server pairs bots with
each other only while at least one other bot stays waiting, so a human joining always finds a bot.

Drafted decks are logged to `$DW_MATCH_LOG_DIR/decks.ndjson` (one `draft` record per deck: id, player, card ids + names; one `match` record per game: deck id, result), so an interesting deck can be looked up by `deck_id` afterwards, e.g. `jq -c 'select(.deck_id==42)' decks.ndjson`.
