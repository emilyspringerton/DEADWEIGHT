# var/matches/

**This directory no longer holds live data.** The real, live production match/deck logs
(`matches.ndjson`/`decks.ndjson`) moved to `~/.local/var/deadweight/matches/` (2026-09-21, S518)
after routine repo-hygiene commands (`git checkout --`/`rm -f`) run against this
git-working-tree path repeatedly truncated/deleted real production data all session, because it
looked like disposable build/test output but was actually the live `dw-server.service`'s
`--match-log` target (`~/.config/deadweight/dw.env`'s `DW_MATCH_LOG_DIR`) the whole time.

**Do not point `--match-log` (or `DW_MATCH_LOG_DIR`/`DEADWEIGHT_DECK_LOG`) at anything inside
this git repo again.** Local dev/test runs of `dw_server` should use a throwaway `--match-log`
directory outside the repo (e.g. `/tmp/...` or a test's own workdir — `tests/test_server_e2e.sh`
already does this correctly), never this path.
