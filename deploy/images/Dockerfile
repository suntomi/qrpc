FROM ubuntu:24.04

# Docker automatically provides architecture-specific build arguments
ARG BUILDARCH
ARG TARGETARCH

# Setup runtime environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV TARGETARCH=${TARGETARCH}

# Install minimal packages for Bazel
RUN --mount=type=cache,target=/var/cache/apt \
    --mount=type=cache,target=/var/lib/apt/lists \
    apt-get update && apt-get install -y \
    curl \
    wget \
    ca-certificates \
    python3 \
    python3-pip \
    unzip \
    git

# Install Bazel via Bazelisk
RUN curl -fsSL https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${BUILDARCH} -o /usr/local/bin/bazel && \
    chmod +x /usr/local/bin/bazel
RUN --mount=type=bind,source=.bazeliskrc,target=.bazeliskrc \
    --mount=type=bind,source=WORKSPACE,target=WORKSPACE \
    --mount=type=cache,target=/root/.cache/bazel \
    bazel version

# Build libqrpc dependency

# Build libqrpc and other artifacts