#!/usr/bin/env python3
"""
training/colab_train.py -- the single "drop into one Colab cell" bootstrap for the DEADWEIGHT league, a direct port of
BRAWLPIT/scripts/colab_train.py (same shape, same lessons).

Paste this whole file into one Colab cell and run it (or, once DEADWEIGHT is cloned, `!python3 training/colab_train.py`).
It clones/updates the repo, builds the real training binary (build/dw_server), installs the RL deps, authenticates
against IDUNA, downloads each league role's newest checkpoint from the shared registry so this run CONTINUES the same
league instead of starting three fresh random networks whenever a Colab runtime recycles, and then runs the 3-role PFSP
league (training/dw_train.py), pushing every generation back to that registry.

Registry auth is IDUNA's M2M agent-secret grant (POST /api/v1/auth/agent, agent DEADWEIGHT-RL): a machine credential,
not a browser OAuth redirect -- the same mechanism every other automated agent in this monorepo uses. Leave the secret
blank for a local-only run (checkpoints stay in league_data/ on the Colab disk).

HONEST STATUS: the game-scoped checkpoint route (/api/v1/game-checkpoints/deadweight) and the DEADWEIGHT-RL agent are in
IDUNA's repo migrations but were NOT deployed on the live IDUNA when this was written (the route 404'd). Until they are,
a registry listing/push fails LOUDLY and training carries on locally -- it never dies for a registry error. Real PPO
training has also never been run before this script (sb3-contrib was not installable in the authoring sandbox), so expect
first-run fixes.

Prereqs: a GitHub personal access token with repo read scope (private repo); optionally the DEADWEIGHT-RL secret.
Tunables (env vars, all optional): DEADWEIGHT_GENERATIONS (default 100000: "until the runtime dies"),
DEADWEIGHT_TIMESTEPS (PPO steps per role per generation, default 4096), DEADWEIGHT_SAVE_FREQ (generations between
snapshots, default 1), DEADWEIGHT_BASE_PORT (default 7100; each role uses its own port), IDUNA_BASE_URL.
"""

import getpass
import os
import subprocess
import sys

REPO_URL_TEMPLATE = "https://{token}@github.com/emilyspringerton/DEADWEIGHT.git"
IDUNA_BASE_URL = os.environ.get("IDUNA_BASE_URL", "https://okemily.com")


def _run(cmd, **kwargs):
    """Never inherit file descriptors (BRAWLPIT S439): a bare subprocess.run() output can vanish in a Colab cell. Pipe the
    child's stdout+stderr and re-emit every line through print(flush=True), which IS reliably visible."""
    print(f"$ {' '.join(cmd)}", flush=True)
    _stream(cmd, **kwargs)


def _stream(cmd, env=None, cwd=None):
    proc = subprocess.Popen(cmd, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    for line in proc.stdout:
        print(line, end="", flush=True)
    proc.wait()
    if proc.returncode != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)
    return proc.returncode


def _bootstrap_repo(github_token):
    """Clone if absent, else force the real latest remote state every run (BRAWLPIT S438: `git pull --ff-only` silently
    leaves a stale checkout). Prints the resulting commit so every report carries its own version proof."""
    if os.path.isdir("DEADWEIGHT"):
        print("DEADWEIGHT already present -- forcing it to the real latest remote state.")
        _run(["git", "-C", "DEADWEIGHT", "fetch", "origin", "main"])
        _run(["git", "-C", "DEADWEIGHT", "reset", "--hard", "origin/main"])
    else:
        _run(["git", "clone", REPO_URL_TEMPLATE.format(token=github_token), "DEADWEIGHT"])
    os.chdir("DEADWEIGHT")
    _run(["git", "log", "--oneline", "-1"])


def _bootstrap_build():
    _run(["apt-get", "-qq", "update"])
    _run(["apt-get", "-qq", "install", "-y", "build-essential"])
    _run(["chmod", "+x", "scripts/build_training.sh"])
    _run(["./scripts/build_training.sh"])
    _run([sys.executable, "-m", "pip", "install", "-q", "gymnasium", "stable-baselines3", "sb3-contrib"])


def main():
    github_token = os.environ.get("GITHUB_TOKEN") or getpass.getpass("GitHub personal access token (repo read scope): ")
    iduna_agent_secret = os.environ.get("IDUNA_AGENT_SECRET") or getpass.getpass(
        "DEADWEIGHT-RL agent secret (blank to skip the shared registry -- local-only run): ")

    _bootstrap_repo(github_token)
    del github_token  # don't keep the token in memory longer than the clone needs it
    _bootstrap_build()
    os.chdir("training")
    sys.path.insert(0, os.getcwd())
    import rl_registry  # noqa: E402 -- only importable after the clone above

    if iduna_agent_secret:
        # Metadata-only public listing (no weights downloaded); shows the newest few generations of EACH role.
        print(f"-- current league standings at {IDUNA_BASE_URL} (before this run adds anything) --", flush=True)
        try:
            by_role = {}
            for c in rl_registry.list_checkpoints(IDUNA_BASE_URL):
                by_role.setdefault(c["role"], []).append(c)
            if not by_role:
                print("  (registry is empty: this run starts the league)")
            for role in ("main", "main_exploiter", "league_exploiter"):
                for c in sorted(by_role.get(role, []), key=lambda c: -c["generation"])[:5]:
                    print(f"  id={c['id']:4d}  {c['role']:18s} gen={c['generation']:3d}  elo={c['elo']:7.1f}  {c.get('name', '')}")
        except Exception as e:  # noqa: BLE001 -- route not deployed / network: say so, keep going
            print(f"  could not list the registry ({e}). Training continues; pushes will warn until IDUNA serves "
                  f"/api/v1/game-checkpoints/deadweight.", flush=True)
    else:
        print("No agent secret given -- this run trains locally only and does NOT join the shared league.")

    cmd = [
        sys.executable, "dw_train.py",
        "--server-bin", "../build/dw_server",
        "--base-port", os.environ.get("DEADWEIGHT_BASE_PORT", "7100"),
        "--generations", os.environ.get("DEADWEIGHT_GENERATIONS", "100000"),
        "--timesteps", os.environ.get("DEADWEIGHT_TIMESTEPS", "4096"),
        "--save-freq", os.environ.get("DEADWEIGHT_SAVE_FREQ", "1"),
        "--league-dir", "league_data",
        "--source-location", "colab",
    ]
    env = os.environ.copy()
    if iduna_agent_secret:
        env["IDUNA_AGENT_SECRET"] = iduna_agent_secret
        # Always resume when a registry is configured (BRAWLPIT S459-71: no separate opt-in flag).
        cmd += ["--registry-url", IDUNA_BASE_URL, "--resume-from-registry"]
    del iduna_agent_secret

    print("\nStarting real training -- runs until --generations or the cell/runtime is stopped.", flush=True)
    _stream(cmd, env=env)  # the long-running process: same pipe-and-reprint fix, or its progress lines vanish


if __name__ == "__main__":
    main()
