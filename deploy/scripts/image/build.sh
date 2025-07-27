#!/bin/bash
set -euo pipefail

type=$1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

# 設定
IMAGE_NAME="suntomi/qrpc"
BUILDER_DOCKER_FILE="${PROJECT_ROOT}/deploy/image/builder/Dockerfile"
DOCKER_FILE="${PROJECT_ROOT}/deploy/image/${type}/Dockerfile"

cd "${PROJECT_ROOT}"

# builder always required other images
echo "==== building builder image for ${type}..."
docker build \
  --progress plain \
  --platform linux/arm64 \
  --build-arg MODE="debug" \
  -f "${BUILDER_DOCKER_FILE}" \
  -t "${IMAGE_NAME}:builder" .

# build actual target image
echo "==== building ${type} image..."
docker build \
  --progress plain \
  --platform linux/arm64 \
  --build-arg BASE_IMAGE="${IMAGE_NAME}:builder" \
  --build-arg MODE="debug" \
  -f "${DOCKER_FILE}" \
  -t "${IMAGE_NAME}:${type}" .

echo "==== building ${type} image done."
