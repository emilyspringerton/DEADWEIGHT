output "load_balancer_ip" {
  description = "External IP of the deadweight-wasm-client Service, once GKE finishes provisioning it. Empty/unknown until the LB is up -- check `kubectl get svc -n deadweight` if this output is blank right after apply."
  value       = try(kubernetes_service.wasm_client.status[0].load_balancer[0].ingress[0].ip, null)
}

output "artifact_registry_repo" {
  description = "Full Artifact Registry repo path images should be pushed to."
  value       = "${var.gcp_region}-docker.pkg.dev/${var.gcp_project}/${google_artifact_registry_repository.deadweight.repository_id}"
}

output "namespace" {
  value = kubernetes_namespace.deadweight.metadata[0].name
}
