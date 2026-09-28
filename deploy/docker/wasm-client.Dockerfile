# DEADWEIGHT web client -- static-file image for Kubernetes.
#
# Interim source: DEADWEIGHT/web/ (the existing, real, working hand-written TypeScript browser
# client -- see web/README.md). This Dockerfile serves whatever `web/` produces today; retarget
# COPY below to the new Emscripten (emcc -sUSE_SDL=2) WASM build's own output directory once that
# lands and its final path is settled (real, live, in-progress work as of 2026-09-28 -- see
# docs/WASM_DEPLOY_NORTHSTAR.md). Not built or run in this sandbox: no `docker` binary here (same
# real, honest gap PRRJECT_FATBABY/docs/northstar/KUBERNETES_MIGRATION.md's own
# `docker/dashboard.Dockerfile` already named) -- verify with a real `docker build` once this runs
# in GitHub Actions or on a box that has Docker.
FROM nginx:1.27-alpine

COPY deploy/docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY web/index.html /usr/share/nginx/html/index.html
COPY web/dist /usr/share/nginx/html/dist

EXPOSE 8080
