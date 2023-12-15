#!/bin/bash
CWD=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
# wscat -c ws://localhost:8888/ws
set -eo pipefail

pushd ${CWD}/webrtc
  node index.js
popd