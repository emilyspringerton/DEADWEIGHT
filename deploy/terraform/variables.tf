# Real, existing identifiers -- NOT provisioned by this module. Confirmed live in
# EMILY/docs/KUBERNETES_SERVICE_MIGRATION_NORTHSTAR.md's own real audit (2026-09-04): GKE
# Autopilot cluster `prrject-fatbaby`, project `project-d24a71e9-2daf-4b2d-917`, `us-central1`.
# This module only ever reads that cluster (a `data` source, never a `resource`) -- per the
# founder's own explicit instruction, it does not create or modify the cluster itself.
variable "gcp_project" {
  description = "Existing GCP project that owns the prrject-fatbaby GKE Autopilot cluster."
  type        = string
  default     = "project-d24a71e9-2daf-4b2d-917"
}

variable "gcp_region" {
  description = "Region for the Artifact Registry repo and the (existing) cluster."
  type        = string
  default     = "us-central1"
}

variable "cluster_name" {
  description = "Existing GKE cluster name (not created here)."
  type        = string
  default     = "prrject-fatbaby"
}

variable "namespace" {
  description = "Kubernetes namespace for the DEADWEIGHT web client -- new, this monorepo's first real k8s workload (see docs/WASM_DEPLOY_NORTHSTAR.md)."
  type        = string
  default     = "deadweight"
}

variable "image_tag" {
  description = "Container image tag to deploy (the CI workflow passes the git SHA)."
  type        = string
  default     = "latest"
}

variable "replicas" {
  description = "Deployment replica count. Low on purpose -- this is a static-file server, not a stateful workload."
  type        = number
  default     = 2
}
