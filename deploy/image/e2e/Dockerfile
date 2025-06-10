# ========================================================================
# stage 1
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# basic packages and bazel installation
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    curl \
    git \
    gnupg2 \
    ninja-build \
    meson \
    python3 \
    python3-pip \
    pkg-config \
    unzip \
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor > /etc/apt/trusted.gpg.d/bazel.gpg && \
    echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list && \
    apt-get update && apt-get install -y bazel && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# separate build for utilizing layer caching
COPY sys/server/ext ./sys/server/ext
COPY tools ./tools
COPY makefile ./makefile
RUN make ext MODE=release

COPY . .
RUN make MODE=release

# ========================================================================
# stage 2
FROM ubuntu:22.04-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN apt-get update && apt-get install -y \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# copy binary and resources from builder stage
COPY --from=builder /workdir/.build/bazel-bin/sys/tests/e2e/server/e2e_server /app/
COPY --from=builder /workdir/sys/tests/e2e/server/resources/ /app/resources/
RUN rm /app/resources/qrpc.bundle.js*
COPY --from=builder /workdir/sys/client/ts/dist/qrpc.bundle.js* /app/resources/
RUN chmod +x /app/e2e_server

EXPOSE 8888 11111

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8888/health || exit 1

# アプリケーション実行
CMD ["./e2e_server"]
