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
| `colab_train.py` (+ `notebooks/dw_league.ipynb`) | one-cell Colab bootstrap (BRAWLPIT's shape): clone, `scripts/build_training.sh`, pip deps, resume from the IDUNA registry, train, push |

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

## Colab

Paste `colab_train.py` into one cell (or run the notebook). No GitHub token needed (public repo, plain https clone). It asks only for the
`DEADWEIGHT-RL` agent secret (optional); with a secret it lists the registry, resumes each role from its newest checkpoint (always,
like BRAWLPIT) and pushes every generation back. `dw_train.py --resume-from-registry` is the flag it uses; registry
listing/push failures print loudly and never stop training (checkpoints stay in `league_data/`).

## Honest status / not done

- **Real PPO training has now run** (2026-09-19, CPU, sb3-contrib 2.9 / torch 2.14): a 2-generation 3-role league against
  the real `dw_server`, checkpoints registered, Elo moving, and a resume-from-registry warm start of all three roles from
  real weights. It was a smoke run (512 timesteps/role), not a trained policy, and has never run *on Colab* itself.
- **The shared registry is live** (2026-09-19): IDUNA was redeployed with `/api/v1/game-checkpoints/deadweight` and the
  `DEADWEIGHT-RL` agent provisioned (secret: `IDUNA/var/agent-secrets.env`, `IDUNA_SECRET_DEADWEIGHT_RL`). A real 3-role
  push and a real resume-from-registry were verified end to end against okemily.com from a local run. Six 512-timestep
  smoke checkpoints (source `colab-test`) sit in the registry from that verification; disable them in NOCK if you don't
  want them as league opponents. Still never run inside a Colab runtime.
- The observation folds the 64 Guild cards into the 9 base-card buckets and the action space is still 5 slots/pass, so a
  trained policy learns Guild cards only by kind/cost tier (a richer observation would change the wire-independent env
  contract and the bot weight blob).
- `--dry-run` uses the fake server (base cards only, scripted opponent), so its Elo attribution is nominal.
- Next: export learned weights to a C-runnable blob (BRAWLPIT `export_policy_weights.py` precedent) so learned bots join
  the live pool; live pool drawing generations from the registry (`rl_bot_pool.py` precedent); parallel envs per role.
