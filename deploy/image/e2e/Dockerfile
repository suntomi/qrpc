# ========================================================================
# stage 1
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC
ARG TARGETARCH
ENV TARGETARCH=${TARGETARCH}

# basic packages and bazel installation
RUN apt-get update && apt-get install -y \
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
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Bazel using Bazelisk
RUN wget -O /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${TARGETARCH} && \
    chmod +x /usr/local/bin/bazel && bazel version

WORKDIR /workspace

# separate build for utilizing layer caching
COPY sys/server/ext ./sys/server/ext
COPY tools ./tools
COPY makefile ./makefile
# make ext will be automatically detect cpu architecture
RUN make ext MODE=release

COPY . .
RUN if [ "${TARGETARCH}" = "arm64" ]; then export ARCH="aarch64"; else export ARCH="x86_64"; fi && \
    make sys MODE=release ARCH=${ARCH}

# ========================================================================
# stage 2
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN apt-get update && apt-get install -y \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# copy binary and resources from builder stage
COPY --from=builder /workspace/.build/bazel-bin/sys/tests/e2e/server/e2e_server /app/
COPY --from=builder /workspace/sys/tests/e2e/server/resources/ /app/resources/
RUN rm -f /app/resources/qrpc.bundle.js*
COPY --from=builder /workspace/sys/client/ts/dist/qrpc.bundle.js* /app/resources/
RUN chmod +x /app/e2e_server

EXPOSE 8888 11111

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8888/health || exit 1

# アプリケーション実行
CMD ["./e2e_server"]
