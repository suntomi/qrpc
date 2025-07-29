#!/bin/bash
set -euo pipefail

svc=$1
env=${2:-local}

if [[ -z "${svc}" ]]; then
  echo "Usage: $0 <service-name>"
  echo "Example: $0 e2e"
  exit 1
fi

# スクリプトのディレクトリを取得
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 設定
NAMESPACE="${svc}"
KUSTOMIZE_DIR="${PROJECT_ROOT}/deploy/k8s/${svc}/${env}"

echo "🚀 Deploying QRPC E2E Server to local Kubernetes..."
echo "  Namespace: ${NAMESPACE}"
echo "  Kustomize directory: ${KUSTOMIZE_DIR}"

# 前提条件の確認
if ! command -v kubectl &> /dev/null; then
    echo "❌ kubectl is not installed or not in PATH"
    exit 1
fi

if ! command -v kustomize &> /dev/null; then
    echo "❌ kustomize is not installed or not in PATH"
    exit 1
fi

# Kubernetes クラスターへの接続確認
if ! kubectl cluster-info &> /dev/null; then
    echo "❌ Cannot connect to Kubernetes cluster"
    echo "💡 Make sure your cluster is running (minikube start, kind create cluster, etc.)"
    exit 1
fi

echo "✅ Kubernetes cluster is accessible"

# Kustomizeを使用してデプロイ
echo "📦 Applying Kubernetes manifests..."
cd "${KUSTOMIZE_DIR}"
kustomize build . | kubectl apply -f -

echo "⏳ Waiting for deployment to be ready..."
kubectl wait --for=condition=available --timeout=300s deployment/e2e-server -n "${NAMESPACE}"

echo "✅ Deployment successful!"

# ステータス確認
echo "📋 Deployment status:"
kubectl get pods,services -n "${NAMESPACE}" -l app=e2e-server

echo ""
echo "🌐 Service information:"
kubectl get service e2e-server-service -n "${NAMESPACE}" -o wide

# ポートフォワーディングの案内
echo ""
echo "🔗 To access the service locally, run:"
echo "  kubectl port-forward -n ${NAMESPACE} service/e2e-server-service 8888:8888 10001:10001"
echo ""
echo "📖 Useful commands:"
echo "  View logs:   kubectl logs -n ${NAMESPACE} -l app=e2e-server -f"
echo "  Scale:       kubectl scale -n ${NAMESPACE} deployment/e2e-server --replicas=2"
echo "  Delete:      kubectl delete -n ${NAMESPACE} -k ${KUSTOMIZE_DIR}"
