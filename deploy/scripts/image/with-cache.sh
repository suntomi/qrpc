#!/bin/bash

set -eou pipefail

CACHE_ROOT="${CACHEDIR}/qrpc"
WORK_ROOT="${WORKDIR:-"/work"}

mkdir -p ${CACHE_ROOT}/${TARGETARCH}/mediasoup

echo "====== ${CACHE_ROOT}/${TARGETARCH}/mediasoup => ${WORK_ROOT} ======"
ls -la ${CACHE_ROOT}/${TARGETARCH}/mediasoup

if [ -d "${CACHE_ROOT}/${TARGETARCH}/mediasoup/out" ]; then
  echo "Using cached mediasoup artifacts"
  mv ${CACHE_ROOT}/${TARGETARCH}/mediasoup/out ${WORK_ROOT}/src/sys/server/ext/mediasoup/worker
fi
if [ -d "${CACHE_ROOT}/${TARGETARCH}/mediasoup/pip_invoke" ]; then
  echo "Using cached mediasoup build tools"
  mv ${CACHE_ROOT}/${TARGETARCH}/mediasoup/pip_invoke ${WORK_ROOT}/src/sys/server/ext/mediasoup/worker
fi

set +e
"${@}"

ret=$?

if [ $ret -ne 0 ]; then
  echo "command[$@] failed with exit code $ret"
fi

if [ -d "${WORK_ROOT}/src/sys/server/ext/mediasoup/worker/out" ]; then
  echo "Cache mediasoup artifacts"
  mv ${WORK_ROOT}/src/sys/server/ext/mediasoup/worker/out ${CACHE_ROOT}/${TARGETARCH}/mediasoup
fi
if [ -d "${WORK_ROOT}/src/sys/server/ext/mediasoup/worker/pip_invoke" ]; then
  echo "Cache mediasoup build tools"
  mv ${WORK_ROOT}/src/sys/server/ext/mediasoup/worker/pip_invoke ${CACHE_ROOT}/${TARGETARCH}/mediasoup
fi

exit $ret