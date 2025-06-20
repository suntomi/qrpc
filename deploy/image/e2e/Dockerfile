# ========================================================================
# stage 1
FROM --platform=${BUILDPLATFORM} ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC
ARG TARGETARCH
ENV TARGETARCH=${TARGETARCH}
ARG BUILDARCH
ENV BUILDARCH=${BUILDARCH}

# basic packages and bazel installation
RUN --mount=type=cache,target=/var/lib/apt/lists \
    apt-get update && apt-get install -y \
    build-essential \
    cmake \
    curl \
    git \
    gnupg2 \
    python3 \
    python3-pip \
    python3-venv \
    pkg-config \
    unzip \
    ca-certificates

# Install Bazel using Bazelisk
RUN curl --output /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${BUILDARCH} && \
    chmod +x /usr/local/bin/bazel && bazel version

WORKDIR /work

# make ext will be automatically detect cpu architecture
RUN --mount=type=bind,rw,source=sys/server/ext,target=/work/src/sys/server/ext \
    --mount=type=bind,source=makefile,target=/work/src/makefile \
    --mount=type=bind,source=scripts,target=/work/src/scripts \
    --mount=type=bind,rw,source=tools,target=/work/src/tools \
    --mount=type=cache,target=/work/artifacts \
    --mount=type=cache,target=/work/src/.build/bazel-cache \
    WORKDIR=/work TARGETARCH=${TARGETARCH} bash /work/src/scripts/image/with-artifacts.sh \
        make -C src ext MODE=release

RUN --mount=type=bind,rw,source=.,target=/work/src \
    --mount=type=cache,target=/work/artifacts \
    --mount=type=cache,target=/work/src/.build/bazel-cache \
    WORKDIR=/work TARGETARCH=${TARGETARCH} bash /work/src/scripts/image/with-artifacts.sh \
        make -C src sys MODE=release PLATFORM=platform_linux_${TARGETARCH} && \
    mv /work/.build/bazel-bin/sys /work/artifacts/${TARGETARCH}/qrpc
# ========================================================================
# stage 2
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC
ARG TARGETARCH
ENV TARGETARCH=${TARGETARCH}

RUN --mount=type=cache,from=builder,target=/var/lib/apt/lists \
    apt-get update && apt-get install -y \
    ca-certificates

WORKDIR /app

# copy binary and resources from builder stage
RUN rm -f /app/resources/qrpc.bundle.js*
RUN --mount=type=bind,from=builder,source=artifacts,target=/tmp/artifacts \
    --mount=type=bind,source=sys,target=/tmp/sys \
    cp /tmp/artifacts/${TARGETARCH}/qrpc/sys/tests/e2e/server/e2e_server /app/ && \
    cp -r /tmp/sys/tests/e2e/server/resources /app

RUN chmod +x /app/e2e_server

EXPOSE 8888 11111

CMD ["./e2e_server"]
