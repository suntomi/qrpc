#!/bin/bash
set -euo pipefail

svc=$1
platform=${2:-linux/arm64}

if [[ -z "${svc}" ]]; then
  echo "Usage: $0 <service-name> [platform]"
  echo "Example: $0 e2e linux/arm64"
  echo "Example: $0 e2e linux/amd64"
  exit 1
fi

# スクリプトのディレクトリを取得
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

# 設定
IMAGE_NAME="qrpc/${svc}-server"
IMAGE_TAG="${3:-latest}"
DOCKER_FILE="${PROJECT_ROOT}/deploy/image/${svc}/Dockerfile"

echo "🔨 Building QRPC3 E2E Server Docker image..."
echo "  Project root: ${PROJECT_ROOT}"
echo "  Platform: ${platform}"
echo "  Image: ${IMAGE_NAME}:${IMAGE_TAG}"
echo "  Dockerfile: ${DOCKER_FILE}"

cd "${PROJECT_ROOT}"

# マルチステージビルドの実行
echo "📦 Building with multi-stage Docker build... $(docker context show)"
docker build \
  --platform ${platform} \
  --progress plain \
  -f "${DOCKER_FILE}" \
  -t "${IMAGE_NAME}:${IMAGE_TAG}" \
  --target runtime \
  .

# 開発用にビルダーイメージも保存（オプション）
echo "📦 Building builder image for development..."
docker build \
  --platform ${platform} \
  -f "${DOCKER_FILE}" \
  -t "${IMAGE_NAME}-builder:${IMAGE_TAG}" \
  --target builder \
  .

echo "✅ Docker images built successfully!"
echo "  Runtime image: ${IMAGE_NAME}:${IMAGE_TAG}"
echo "  Builder image: ${IMAGE_NAME}-builder:${IMAGE_TAG}"

# イメージの情報を表示
echo "📋 Image information:"
docker images "${IMAGE_NAME}:${IMAGE_TAG}"
docker images "${IMAGE_NAME}-builder:${IMAGE_TAG}"

echo "🎉 Build completed!"
