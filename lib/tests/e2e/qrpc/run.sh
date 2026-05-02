#!/bin/bash

set -eo pipefail

CWD=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
ROOT=$(cd "${CWD}/../../../.." && pwd)
QRPC_SERVER_BIN="${ROOT}/.build/bazel-bin/lib/tests/e2e/qrpc/server/e2e_qrpc_server"
QRPC_CLIENT_BIN="${ROOT}/.build/bazel-bin/lib/tests/e2e/qrpc/client/e2e_qrpc_client"
source ${CWD}/../../tools/debugger.sh

setup_server "${QRPC_SERVER_BIN}"

with_dbg "${QRPC_CLIENT_BIN}" "$@"
