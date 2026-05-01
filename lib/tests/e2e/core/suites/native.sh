#!/bin/bash

set -eo pipefail

CWD=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
ROOT="${CWD}/../../../../.."
source ${CWD}/../../../tools/debugger.sh

with_dbg ${ROOT}/.build/bazel-bin/lib/tests/e2e/core/client/e2e_client_native "$@"

