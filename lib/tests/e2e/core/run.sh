#!/bin/bash

set -uo pipefail

CWD=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "${CWD}/../../../.." && pwd)
SERVER_BIN="${ROOT}/.build/bazel-bin/lib/tests/e2e/core/server/e2e_server"
SUITES_DIR="${CWD}/suites"

source "${ROOT}/lib/tests/tools/debugger.sh"

SERVER_PID=""
DEBUGGER_PID=""
FAILED_SUITES=()
REQUESTED_SUITE="${1:-}"

cleanup() {
  echo "cleaning up... (server pid=${SERVER_PID}, debugger pid=${DEBUGGER_PID})"
  SERVER_PID=$(pgrep -f -x "${SERVER_BIN}" | head -n 1 || true)
  if [ -n "${SERVER_PID}" ] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "terminating server (pid=${SERVER_PID})..."
    kill -TERM "${SERVER_PID}" 2>/dev/null || true
  fi
  if [ -n "${DEBUGGER_PID}" ] && kill -0 "${DEBUGGER_PID}" 2>/dev/null; then
    echo "terminating debugger (pid=${DEBUGGER_PID})..."
    wait "${DEBUGGER_PID}" 2>/dev/null || true
  fi
}

trap cleanup EXIT

with_dbg "${SERVER_BIN}" \
  > >(sed $'s/^/\033[32m[e2e_server] /; s/$/\033[0m/') \
  2> >(sed $'s/^/\033[32m[e2e_server] /; s/$/\033[0m/' >&2) &
DEBUGGER_PID=$!

echo "waiting for server to start..."
for _ in {1..50}; do
  SERVER_PID=$(pgrep -f -x "${SERVER_BIN}" | head -n 1 || true)
  if [ -n "${SERVER_PID}" ]; then
    break
  fi
  if ! kill -0 "${DEBUGGER_PID}" 2>/dev/null; then
    echo "failed to launch ${SERVER_BIN}"
    exit 1
  fi
  sleep 0.5
done

if [ -z "${SERVER_PID}" ]; then
  echo "failed to find server pid for ${SERVER_BIN}"
  exit 1
fi
echo "server pid = ${SERVER_PID}"

while IFS= read -r suite; do
  suite_name=$(basename "${suite}" .sh)
  if [ -n "${REQUESTED_SUITE}" ] && [ "${suite_name}" != "${REQUESTED_SUITE}" ]; then
    continue
  fi
  echo "running $(basename "${suite}")"
  if ! bash "${suite}"; then
    FAILED_SUITES+=("$(basename "${suite}")")
  fi
done < <(find "${SUITES_DIR}" -maxdepth 1 -type f -name '*.sh' | sort)

if [ -n "${REQUESTED_SUITE}" ] && [ "${#FAILED_SUITES[@]}" -eq 0 ]; then
  if ! find "${SUITES_DIR}" -maxdepth 1 -type f -name "${REQUESTED_SUITE}.sh" | grep -q .; then
    echo "suite not found: ${REQUESTED_SUITE}"
    exit 1
  fi
fi

if [ "${#FAILED_SUITES[@]}" -gt 0 ]; then
  echo "failed suites:"
  printf '  %s\n' "${FAILED_SUITES[@]}"
  exit 1
fi

echo "all suites passed"
