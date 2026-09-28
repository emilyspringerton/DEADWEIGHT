# DEADWEIGHT web client -- Kubernetes resources on the EXISTING prrject-fatbaby GKE Autopilot
# cluster. This module intentionally never touches the cluster itself (no `google_container_
# cluster` resource, only a `data` read of it) -- founder real-time, 2026-09-28: "(not for the
# cluster that exists for the deployments)". See docs/WASM_DEPLOY_NORTHSTAR.md for the full
# rationale, the cluster's real known-broken-node-scheduling history, and what's still manual.

provider "google" {
  project = var.gcp_project
  region  = var.gcp_region
}

data "google_client_config" "default" {}

data "google_container_cluster" "prrject_fatbaby" {
  name     = var.cluster_name
  location = var.gcp_region
  project  = var.gcp_project
}

provider "kubernetes" {
  host                   = "https://${data.google_container_cluster.prrject_fatbaby.endpoint}"
  token                  = data.google_client_config.default.access_token
  cluster_ca_certificate = base64decode(data.google_container_cluster.prrject_fatbaby.master_auth[0].cluster_ca_certificate)
}

# Real Artifact Registry repo for this image -- the one piece of GCP infra this module DOES
# create, since it's specific to this workload, not the shared cluster.
resource "google_artifact_registry_repository" "deadweight" {
  project       = var.gcp_project
  location      = var.gcp_region
  repository_id = "deadweight"
  format        = "DOCKER"
  description   = "DEADWEIGHT web client container images (WASM/JS/HTML static server)."
}

resource "kubernetes_namespace" "deadweight" {
  metadata {
    name   = var.namespace
    labels = { "app.kubernetes.io/part-of" = "deadweight" }
  }
}

resource "kubernetes_deployment" "wasm_client" {
  metadata {
    name      = "deadweight-wasm-client"
    namespace = kubernetes_namespace.deadweight.metadata[0].name
    labels    = { app = "deadweight-wasm-client" }
  }

  spec {
    replicas = var.replicas

    selector {
      match_labels = { app = "deadweight-wasm-client" }
    }

    template {
      metadata {
        labels = { app = "deadweight-wasm-client" }
      }

      spec {
        container {
          name  = "wasm-client"
          image = "${var.gcp_region}-docker.pkg.dev/${var.gcp_project}/${google_artifact_registry_repository.deadweight.repository_id}/wasm-client:${var.image_tag}"

          port {
            container_port = 8080
          }

          resources {
            requests = { cpu = "100m", memory = "64Mi" }
            limits   = { cpu = "500m", memory = "256Mi" }
          }

          readiness_probe {
            http_get {
              path = "/healthz"
              port = 8080
            }
            initial_delay_seconds = 3
            period_seconds        = 10
          }

          liveness_probe {
            http_get {
              path = "/healthz"
              port = 8080
            }
            initial_delay_seconds = 10
            period_seconds        = 30
          }
        }
      }
    }
  }
}

# type=LoadBalancer -- the simplest real, working public entry point on GKE (a real external IP,
# no separate Ingress controller to stand up first). Stitching wotan.okemily.com/DEADWEIGHT into
# this IP (an nginx `proxy_pass` on the existing WOTAN vhost, on the VPS, outside this cluster) is
# a real, named, NOT-yet-done follow-up -- see docs/WASM_DEPLOY_NORTHSTAR.md.
resource "kubernetes_service" "wasm_client" {
  metadata {
    name      = "deadweight-wasm-client"
    namespace = kubernetes_namespace.deadweight.metadata[0].name
  }

  spec {
    selector = { app = "deadweight-wasm-client" }
    type     = "LoadBalancer"

    port {
      port        = 80
      target_port = 8080
    }
  }
}
