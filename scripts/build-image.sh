#!/usr/bin/env bash
# build-image.sh [TAG] — Cloud Build the deadweight image: deployed dw_server/dw_bot (stopgap, see ops/docker/deadweight.Dockerfile) + web/bridge.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-$(git -C "$SRC" rev-parse --short HEAD)}"
PROJECT="${PROJECT:-project-d24a71e9-2daf-4b2d-917}"
BIN="${DW_BIN_DIR:-$HOME/.local/opt/deadweight/bin}"
CTX="$(mktemp -d)"; trap 'rm -rf "$CTX"' EXIT
mkdir -p "$CTX/bridge"
cp "$SRC/web/bridge/package.json" "$SRC/web/bridge/package-lock.json" "$SRC/web/bridge/ws-tcp-bridge.js" "$CTX/bridge/"
cp "$BIN/dw_server" "$BIN/dw_bot" "$CTX/"
cp "$SRC/ops/docker/deadweight.Dockerfile" "$CTX/Dockerfile"
# Same CI-safe submit as IDUNA/scripts/build-image.sh (found live 2026-10-05): the CI SA (github-ci,
# EMILY/gitops/CI_SETUP.md) is scoped to cloudbuild.builds.editor + artifactregistry.writer, so pin the
# staging bucket (skips a project-wide buckets.list) and go --async + poll status (it cannot stream logs).
BUILD_ID=$(gcloud builds submit "$CTX" --project "$PROJECT" \
  --tag "us-central1-docker.pkg.dev/$PROJECT/emily/deadweight:$TAG" \
  --gcs-source-staging-dir="gs://${PROJECT}_cloudbuild/source" \
  --async --format="value(id)")
echo "submitted build $BUILD_ID, polling for completion..."
while true; do
  STATUS=$(gcloud builds describe "$BUILD_ID" --project "$PROJECT" --format="value(status)")
  case "$STATUS" in
    SUCCESS) echo "deadweight:$TAG"; exit 0 ;;
    FAILURE|INTERNAL_ERROR|TIMEOUT|CANCELLED|EXPIRED) echo "build $BUILD_ID: $STATUS" >&2; exit 1 ;;
    *) sleep 5 ;;
  esac
done
