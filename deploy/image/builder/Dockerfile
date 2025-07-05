FROM --platform=${BUILDPLATFORM} ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC
ARG TARGETARCH
ENV TARGETARCH=${TARGETARCH}
ARG BUILDARCH
ENV BUILDARCH=${BUILDARCH}

# basic packages and bazel installation
RUN --mount=type=cache,target=/var/lib/apt/lists \
    apt-get update && apt-get install -y \
    build-essential linux-libc-dev \
    cmake \
    curl \
    git \
    gnupg2 \
    python3 \
    python3-pip \
    python3-venv \
    pkg-config \
    unzip \
    ca-certificates \
    rsync

# Install Bazel using Bazelisk
RUN curl --output /usr/local/bin/bazel -L https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${BUILDARCH} && \
    chmod +x /usr/local/bin/bazel

WORKDIR /work

# preinstall required bazel version
RUN --mount=type=bind,source=.bazeliskrc,target=/work/.bazeliskrc \
    --mount=type=cache,target=/root/.cache \
    --mount=type=bind,source=WORKSPACE,target=/work/WORKSPACE \
    bazel version

# make ext will be automatically detect cpu architecture
RUN --mount=type=bind,rw,source=sys/server/ext,target=/work/src/sys/server/ext \
    --mount=type=bind,source=makefile,target=/work/src/makefile \
    --mount=type=bind,source=deploy/scripts,target=/work/src/deploy/scripts \
    --mount=type=bind,rw,source=tools,target=/work/src/tools \
    --mount=type=cache,target=/work/artifacts \
    WORKDIR=/work TARGETARCH=${TARGETARCH} bash /work/src/deploy/scripts/image/with-artifacts.sh \
        make -C src ext MODE=release

RUN --mount=type=bind,rw,source=.,target=/work/src \
    --mount=type=cache,target=/work/artifacts \
    --mount=type=cache,target=/root/.cache \
    WORKDIR=/work TARGETARCH=${TARGETARCH} bash /work/src/deploy/scripts/image/with-artifacts.sh \
        make -C src sys MODE=release PLATFORM=linux_${TARGETARCH} && \
        rsync -av --delete /work/src/.build/bazel-bin/ /build/
