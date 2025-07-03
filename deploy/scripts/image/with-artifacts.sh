#!/bin/bash

set -eou pipefail

mkdir -p ${WORKDIR}/artifacts/${TARGETARCH}/mediasoup

echo "====== dir ${WORKDIR}/artifacts/${TARGETARCH}/mediasoup ======"
ls -la ${WORKDIR}/artifacts/${TARGETARCH}/mediasoup

if [ -d "${WORKDIR}/artifacts/${TARGETARCH}/mediasoup/out" ]; then
  echo "Using cached mediasoup artifacts"
  mv ${WORKDIR}/artifacts/${TARGETARCH}/mediasoup/out ${WORKDIR}/src/sys/server/ext/mediasoup/worker
fi
if [ -d "${WORKDIR}/artifacts/${TARGETARCH}/mediasoup/pip_invoke" ]; then
  echo "Using cached mediasoup build tools"
  mv ${WORKDIR}/artifacts/${TARGETARCH}/mediasoup/pip_invoke ${WORKDIR}/src/sys/server/ext/mediasoup/worker
fi

set +e
"${@}"

ret=$?

if [ $ret -ne 0 ]; then
  echo "command[$@] failed with exit code $ret"
fi

if [ -d "/work/src/sys/server/ext/mediasoup/worker/out" ]; then
  echo "Cache mediasoup artifacts"
  mv /work/src/sys/server/ext/mediasoup/worker/out /work/artifacts/${TARGETARCH}/mediasoup
fi
if [ -d "/work/src/sys/server/ext/mediasoup/worker/pip_invoke" ]; then
  echo "Cache mediasoup build tools"
  mv /work/src/sys/server/ext/mediasoup/worker/pip_invoke /work/artifacts/${TARGETARCH}/mediasoup
fi

exit $ret