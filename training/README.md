# DEADWEIGHT training league (S503-08)

BRAWLPIT's packet-level autocurriculum, 1-for-1, for card mode: the env is a real client of `docs/WIRE_PROTOCOL.md`
(so a trained policy is a drop-in bot), PFSP league with three roles (MAIN / MAIN_EXPLOITER / LEAGUE_EXPLOITER), one
snapshot cycle registers all three, Elo per checkpoint, checkpoints pushed to IDUNA's game-scoped registry
(`docs/IDUNA_CONTRACT.md`, `/api/v1/game-checkpoints/deadweight`).

| File | What |
|---|---|
| `rl_league.py` (+ tests) | verbatim port of BRAWLPIT's (PFSP, Elo, `register_generation_snapshot`) |
| `rl_registry.py` | stdlib IDUNA client (`DEADWEIGHT-RL` agent), `LocalRegistry` no-IDUNA fallback, `make_registry()` |
| `dw_rules.py` | Python port of PARENA `card_rules.prn`; `test_dw_rules.py` replays all `tests/parity_vectors.txt` |
| `dw_wire.py` | frame codec |
| `dw_env.py` | gymnasium-style env; obs/action/reward spec in its header; `--smoke-test --port N` needs no gymnasium |
| `fake_server.py` | in-process pure-Python server (scripted opponent) so tests need no C binary |
| `dw_train.py` | 3-role orchestrator (MaskablePPO, explicit `ent_coef`), `--dry-run` |
| `notebooks/dw_league.ipynb` | Colab stub |

## Run

```bash
cd training
python3 -m unittest discover -p "test_*.py"                     # no third-party packages needed
python3 dw_train.py --dry-run --generations 3 --episodes 10     # league/registry/Elo wiring, random policy, fake server
pip install gymnasium stable-baselines3 sb3-contrib             # real training deps (not in the authoring sandbox)
python3 dw_train.py --server-bin ../build/dw_server --generations 50 --timesteps 4096 [--registry-url URL --agent-secret S]
```

## Relation to the live bot pool

Same `dw_server` binary, different deployment: training runs three private `--fast-forward` servers on their own ports
(`--base-port`, +1, +2) with their own league dir; the live pool is separate standing `dw_bot` processes on the
public server. Per role, an `OpponentBot` (kind=human, so the server's "last waiting bot is reserved for a human" rule
can't stall the pair) plays the PFSP-sampled league member against the trainee (kind=bot).

## Honest status / not done

- **Real PPO training has not been run** (sb3-contrib/gymnasium not installable in the sandbox). Code follows their
  documented APIs (`MaskablePPO`, `action_masks()`); expect first-run fixes on Colab.
- `--dry-run` uses the fake server's built-in scripted opponent, so its Elo attribution is nominal; real Elo
  attribution needs the real server + `OpponentBot` path.
- Next: export learned weights to a tiny C/Java-runnable blob (BRAWLPIT `export_policy_weights.py` precedent) so
  learned bots join the live pool; live pool drawing all generations from the registry (`rl_bot_pool.py` precedent);
  parallel envs per role; IDUNA `match-result`-style Elo through the registry's `/match-result` route.
