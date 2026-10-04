# DEADWEIGHT on GKE (K8S-MV-04): dw_server + dw_bot (the exact deployed binaries, libc-only) + the Node ws<->tcp bridge.
# STOPGAP: binaries are the deployed ~/.local/opt/deadweight/bin builds (scripts/build-image.sh stages them), not a Bazel build in
# Cloud Build; replace with a hermetic build step. The bridge is the existing Node relay moved as-is.
FROM node:20-bookworm-slim
WORKDIR /app
COPY bridge/package.json bridge/package-lock.json /app/bridge/
RUN cd /app/bridge && npm ci --omit=dev
COPY bridge/ws-tcp-bridge.js /app/bridge/
COPY dw_server dw_bot /app/
RUN chmod 755 /app/dw_server /app/dw_bot
