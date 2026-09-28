# DEADWEIGHT web client — GitOps deploy to GKE (NORTHSTAR, 2026-09-28)

## Source

Founder real-time (routed via `emily observe`, Apple #21168, session `sess-20260923-1030-4a526255`):
"spin up an agent to start figuring out auto deploy upon release new functionality figure out auto
deploy from github for the wasm version set it up on kubernetes we have a kubernetes cluster in
GCLOUD so the web server for the frontend for DEADSPACE needs to be our first kubernetes pod - git
ops so it needs to be terraform or helm or whatever the fuck infrastructure as code (not for the
cluster that exists for the deployments)". "DEADSPACE" is a slip for DEADWEIGHT — the whole
surrounding session is about DEADWEIGHT's new WASM client, in progress in parallel to this doc,
separate from the existing hand-written TS browser client already in `web/`. **Correction, same
session, right after this fork was launched**: the founder rejected the initial Emscripten-based
approach ("do not use emscripten it doesnt work it needs to be native wasm like mixforge") —
the real client is a native wasm32-unknown-unknown build (plain `clang -target wasm32-unknown-
unknown -nostdlib` + `wasm-ld`, zero Emscripten SDK/JS runtime, mirroring `MIXFORGE/web/room.wasm`/
`dsp.wasm`'s own pipeline). See `docs/NATIVE_WASM_CLIENT_NORTHSTAR.md` for that work. This doc's
mentions of "the Emscripten build" below are now stale in that one respect; the deploy pipeline
itself (CI → container → Kubernetes) is unaffected — it just needs to serve whatever static
output directory the native build lands in (`web/dist/generated/` today — a build output,
gitignored, part of the existing `web/` static site — not a standalone HTML/JS host).

This was worked as a background fork alongside that parallel WASM-client-build work — this doc
covers only the deploy pipeline (CI → container → Kubernetes), not the client itself.

## Real, checked-live state (not assumed)

- **The cluster genuinely already exists.** `EMILY/docs/KUBERNETES_SERVICE_MIGRATION_NORTHSTAR.md`
  (2026-09-04 audit): GKE Autopilot cluster `prrject-fatbaby`, project
  `project-d24a71e9-2daf-4b2d-917`, `us-central1`, `status: RUNNING`. Confirmed via that doc's own
  live `gcloud container clusters describe` / `kubectl` session at the time.
- **That same audit found the cluster non-functional for any real workload**: every pod
  cluster-wide (including GKE's own required system pods — `kube-dns`, `metrics-server`, etc.)
  stuck `Pending`, the Autopilot autoscaler reporting `autoscaledNodesTarget: 0` for 32+ continuous
  hours despite real, unmet pod demand. Root cause was NOT resolved — ruled out quota, IP
  exhaustion, and cluster-level config errors; the real remaining suspects (a regional capacity
  shortage for Autopilot's chosen machine shape, or a deeper platform issue) need GCP Console
  access or a support case, neither of which is possible from this sandbox. **This status has not
  been re-checked since** — it may have resolved itself, or may not have. Whoever runs the pipeline
  in this doc for real should check `kubectl get nodes` first and not assume the cluster is healthy
  just because Terraform apply succeeds (Terraform creating a Deployment object doesn't require a
  schedulable node — the pod can still sit Pending forever, exactly like every other pod did during
  that 32-hour window).
- **No live GCP credentials exist in this sandbox right now** (`gcloud auth list` → "No
  credentialed accounts"). The founder's own prior authenticated session that provisioned the
  cluster was a one-time interactive login (`gcloud auth login` can't be done non-interactively
  from a Claude Code session) — it's gone. This pass could not run `terraform plan`/`apply` for
  real, and did not fabricate a "looks like it worked" result.
- **No `docker` or `kubectl` binary is installed in this sandbox.** `terraform` is (v1.16.4) — used
  for real `init`/`validate` below. Same honest gap `PRRJECT_FATBABY/docs/northstar/
  KUBERNETES_MIGRATION.md`'s own `docker/dashboard.Dockerfile` already named for its own, earlier,
  unrelated Dockerfile.
- **This would be the first real Kubernetes workload anywhere in this monorepo.** Every other live
  service (`iduna`, MIXFORGE's room server, `jewel-jupyter`, all 46+ systemd units the
  `KUBERNETES_SERVICE_MIGRATION_NORTHSTAR.md` audit found) runs as a `systemctl --user` unit on one
  VPS. `prrject-fatbaby` exists but, per the audit above, has never actually run a real workload
  successfully (it never got past "cluster provisioned, no nodes ever came up"). Naming this
  plainly, not implying otherwise.

## What this pass built (real, verified where verification was possible)

- **`deploy/docker/wasm-client.Dockerfile`** + **`deploy/docker/nginx.conf`** — a minimal
  `nginx:1.27-alpine` image serving DEADWEIGHT's web client as static files, listening on 8080 (a
  real, deliberate choice for a non-root-friendly K8s container), with `application/wasm`
  registered as a real MIME type (nginx's stock `mime.types` doesn't include it — without this fix,
  browsers get `application/octet-stream` and silently fall back to a slower non-streaming
  WebAssembly compile path) and gzip enabled for `.wasm`/`.js`/`.json`. **Interim source**: copies
  `web/index.html` + `web/dist/` (the existing, real, working hand-written TypeScript browser
  client, see `web/README.md`) — NOT the new Emscripten WASM build, which is separate, parallel,
  in-progress work as of this same date. Retarget the `COPY` lines once that build's own output
  directory is finalized. **Not built or run** — no `docker` binary in this sandbox; syntax-checked
  by hand only.
- **`deploy/terraform/`** (`versions.tf`, `variables.tf`, `main.tf`, `outputs.tf`) — a real
  Terraform module for the Kubernetes resources ONLY: a new `deadweight` namespace, a `Deployment`
  (2 replicas, resource requests/limits, readiness/liveness probes against a new `/healthz`
  location added in `nginx.conf`), and a `Service` of `type=LoadBalancer` for a real public IP with
  no separate ingress controller needed first. The cluster itself is read via a `data
  "google_container_cluster"` block, **never** a `resource` — per the founder's own explicit
  instruction not to touch the cluster that already exists. The one real new piece of GCP infra
  this module DOES create is an Artifact Registry Docker repo (`deadweight`) to push images into,
  since that's specific to this workload, not shared cluster state.
  **Verified for real**: `terraform init -backend=false` (no state backend configured yet — a real,
  named follow-up, not a decision; local state is fine for a single-operator first pass but should
  move to a GCS backend before this is treated as production IaC) and `terraform validate` — both
  clean, zero errors, against real provider schemas (`hashicorp/google` ~>6.0, `hashicorp/
  kubernetes` ~>2.32). **Not verified**: `terraform plan`/`apply` against the real cluster — no
  credentials available this pass (see above).
- **`.github/workflows/deploy-wasm.yml`** — the GitOps trigger itself: on push to `main` (path-
  filtered to `web/**`/`deploy/**`) or manual `workflow_dispatch`, builds the web client, auths to
  GCP via `google-github-actions/auth` (a `DEADWEIGHT_GCP_SA_KEY` repo secret — **does not exist
  yet**, real human step below), pushes the image to Artifact Registry, fetches GKE credentials,
  and runs `terraform apply`. This — a CI job directly applying Terraform on every push — IS a
  real, legitimate GitOps shape for a single-cluster, single-operator setup; it is NOT a
  pull-based-controller GitOps setup (no ArgoCD/Flux). Naming that distinction since "git ops" can
  imply the latter and the founder didn't ask for a controller by name; standing up one wasn't
  judged worth the added complexity for a single static-file workload. **Verified**: real YAML
  parse (`python3 -c "import yaml; yaml.safe_load(...)"`) — clean. **Not verified**: an actual CI
  run (blocked on the missing secret + the cluster health question above; `workflow_dispatch`-only
  trigger is deliberate so a human runs this once, on purpose, rather than it firing automatically
  on the next unrelated `web/` commit before those are resolved — reconsider adding it back to the
  normal `push` path once a real run has succeeded once).

## Real, honest, NOT done this pass

- **`DEADWEIGHT_GCP_SA_KEY` GitHub secret** — needs a human with live `gcloud` access to create a
  real service account (`roles/artifactregistry.writer` + `roles/container.developer` scoped to
  `project-d24a71e9-2daf-4b2d-917`), mint a JSON key, and add it as a repo secret. Same "blocked on
  a human-only step" shape as IDUNA's Google OAuth devportal gate and `GITHUB_TOKEN`'s missing
  repo-admin scope elsewhere in this monorepo — named, not silently skipped.
- **Cluster health re-check** — the 32-hour zero-nodes finding is real but three-plus weeks old as
  of this doc; it needs a fresh `kubectl get nodes` (or the founder checking the GKE Console) before
  anyone trusts a deploy from this pipeline to actually serve traffic, not just create objects.
- **`wotan.okemily.com/DEADWEIGHT` routing** — the founder's stated target URL. This pipeline gets
  the client a real public IP (the `LoadBalancer` Service), but stitching that IP into the existing
  WOTAN static site (which lives on the VPS, entirely outside this GKE cluster) needs a new
  location block on WOTAN's own live nginx vhost (`WOTAN/ops/`) proxying `/DEADWEIGHT` to that IP —
  a real, separate, small change, deliberately not made here since it touches WOTAN's live,
  currently-working nginx config and the LB IP doesn't exist yet to proxy to. Real next step once
  the LB is up: `location /DEADWEIGHT/ { proxy_pass http://<load_balancer_ip>/; }` (or a DNS-level
  approach if a dedicated subdomain is preferred instead of a path).
- **Terraform state backend** — currently local-only (fine for this first pass, a real gap before
  more than one person/CI-run ever applies this module: two concurrent `apply`s with local state
  can corrupt each other). A GCS backend bucket (this monorepo already has one for FatBaby's
  own backups, per `KUBERNETES_SERVICE_MIGRATION_NORTHSTAR.md`'s own Stage 0 note) is the natural
  next step, not built here.
- **IDUNA SSO wiring inside the WASM client itself** — real, live, already-verified endpoints exist
  to build this against (`iam.okemily.com` redirect + DEADWEIGHT's own `sso-exchange` route, see
  `WOTAN/CLAUDE.md`'s "Auth — IDUNA as SSO" section) — that's client-code work, explicitly out of
  scope for this deploy-pipeline pass (owned by the parallel, in-progress Emscripten build work).
- **Retargeting the Docker build off `web/`** once the real WASM build's output directory exists.

## Phased plan

1. **(this pass)** Real Terraform + Dockerfile + CI workflow, validated syntactically, honest about
   what's unverified.
2. Human: create `DEADWEIGHT_GCP_SA_KEY`, re-check cluster node health.
3. First real `workflow_dispatch` run once 2 is done — verify the image builds, pushes, and the pod
   actually reaches `Running` (not just `Pending`).
4. Wire `wotan.okemily.com/DEADWEIGHT` → the LB IP on WOTAN's nginx.
5. Move Terraform state to a real GCS backend; consider re-enabling the automatic `push` trigger
   once step 3 has succeeded at least once by hand.
6. Retarget the Docker build to the real Emscripten WASM output once that work lands, and wire the
   IDUNA SSO exchange into the client itself.
