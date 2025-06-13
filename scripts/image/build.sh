#!/bin/bash
set -euo pipefail

# スクリプトのディレクトリを取得
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 設定
IMAGE_NAME="qrpc/e2e-server"
IMAGE_TAG="${1:-latest}"
DOCKER_FILE="${PROJECT_ROOT}/deploy/image/e2e/Dockerfile"

echo "🔨 Building QRPC3 E2E Server Docker image..."
echo "  Project root: ${PROJECT_ROOT}"
echo "  Image: ${IMAGE_NAME}:${IMAGE_TAG}"
echo "  Dockerfile: ${DOCKER_FILE}"

cd "${PROJECT_ROOT}"

# マルチステージビルドの実行
# echo "📦 Building with multi-stage Docker build..."
# docker build \
#   --progress plain \
#   --platform linux/arm64 \
#   -f "${DOCKER_FILE}" \
#   -t "${IMAGE_NAME}:${IMAGE_TAG}" \
#   --target runtime \
#  .

# 開発用にビルダーイメージも保存（オプション）
echo "📦 Building builder image for development..."
docker build \
  --progress plain \
  --platform linux/arm64 \
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
