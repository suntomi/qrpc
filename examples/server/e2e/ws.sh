#!/bin/bash
CWD=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
# wscat -c ws://localhost:8888/ws
set -eo pipefail
if [ ! -x ${CWD}/websocat ]; then
  echo "install websocat..."
  case $(uname -m) in
    x86_64) arch="x86_64" ;;
    arm64|aarch64) arch="aarch64" ;;
    *) echo "unsuported arch: $(uname -m)" && exit 1 ;;
  esac
  case $(uname -s) in
    Darwin) os="apple-darwin" ;;
    Linux) os="unknown-linux-musl" ;;
    *) echo "unsuported os: $(uname -s)" && exit 1 ;;
  esac
  curl -L "https://github.com/vi/websocat/releases/download/v1.12.0/websocat.${arch}-${os}" \
    -o ${CWD}/websocat
  chmod +x ${CWD}/websocat
fi

test() {
  for i in {0..15}; do
    len=$((2 << i))
    echo "send ${len} bytes payload..." >&2
    randstr=$(openssl rand -base64 ${len} | paste -d' ' -s - | tr -d ' ')
    command=${randstr:0:${len}}
    echo "$command"
    read -r response
    if [ "$command" != "$response" ]; then
      path=${CWD}/error-${len}.txt
      echo "${command}" > ${path}
      echo "${response}" > ${path}
      echo "Unexpected response: see ${path}" >&2
      break
    fi
    echo "OK" >&2
  done
  kill "$PPID"
}

export -f test
export CWD
# ${CWD}/websocat ws://ws.vi-server.org/mirror --binary sh-c:'exec bash -c test'
${CWD}/websocat ws://127.0.0.1:8888/ws --binary sh-c:'exec bash -c test'
