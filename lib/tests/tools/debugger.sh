#!/bin/bash

DEBUGGER_CWD=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)

with_dbg() {
  case $(uname -s) in
    Darwin) lldb --batch --source-quietly \
      -o run \
      -k 'thread backtrace all' \
      -k 'quit 1' \
      -- "$@" ;;
    Linux) echo "TODO: unsuported os: $(uname -s)" && exit 1 ;;
    *) echo "unsuported os: $(uname -s)" && exit 1 ;;
  esac
}

cleanup_server() {
  echo "cleaning up... (server pid=${SERVER_PID}, debugger pid=${DEBUGGER_PID})"
  if [ -n "${SERVER_PID}" ] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "terminating server (pid=${SERVER_PID})..."
    kill -TERM "${SERVER_PID}" 2>/dev/null || true
  fi
  if [ -n "${DEBUGGER_PID}" ] && kill -0 "${DEBUGGER_PID}" 2>/dev/null; then
    echo "terminating debugger (pid=${DEBUGGER_PID})..."
    wait "${DEBUGGER_PID}" 2>/dev/null || true
  fi
}

setup_server() {
  local server_bin="${1}"
  SERVER_PID=""
  DEBUGGER_PID=""

  trap cleanup_server EXIT

  with_dbg "${server_bin}" \
    > >(sed $'s/^/\033[32m[e2e_server] /; s/$/\033[0m/') \
    2> >(sed $'s/^/\033[32m[e2e_server] /; s/$/\033[0m/' >&2) &
  DEBUGGER_PID=$!

  echo "waiting for server to start..."
  for _ in {1..50}; do
    SERVER_PID=$(pgrep -f -x "${server_bin}" | head -n 1 || true)
    if [ -n "${SERVER_PID}" ]; then
      break
    fi
    if ! kill -0 "${DEBUGGER_PID}" 2>/dev/null; then
      echo "failed to launch ${server_bin}"
      exit 1
    fi
    sleep 0.5
  done

  if [ -z "${SERVER_PID}" ]; then
    echo "failed to find server pid for ${server_bin}"
    exit 1
  fi
  echo "server pid = ${SERVER_PID}"
}
