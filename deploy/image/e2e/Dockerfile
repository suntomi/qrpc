ARG BASE_IMAGE=ubuntu:24.04
FROM ${BASE_IMAGE} AS builder

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC
ARG TARGETARCH
ENV TARGETARCH=${TARGETARCH}

RUN --mount=type=cache,from=builder,source=/var/lib/apt/lists,target=/var/lib/apt/lists \
    apt-get update && apt-get install -y \
    ca-certificates libasan8

WORKDIR /app
ENV RSC_ROOT=/app

# copy binary and resources from builder stage
COPY --from=builder /build/sys/tests/e2e/server/e2e_server /app
RUN --mount=type=bind,source=sys,target=/tmp/sys \
    cp -r /tmp/sys/tests/e2e/server/resources /app && \
    rm -f /app/resources/qrpc.bundle.* && \
    cp -f /tmp/sys/client/ts/dist/qrpc.bundle.* /app/resources/

RUN chmod +x /app/e2e_server

EXPOSE 8888 11111

CMD ["./e2e_server"]
