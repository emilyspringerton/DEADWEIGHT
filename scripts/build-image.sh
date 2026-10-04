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
gcloud builds submit "$CTX" --project "$PROJECT" --tag "us-central1-docker.pkg.dev/$PROJECT/emily/deadweight:$TAG"
echo "deadweight:$TAG"
