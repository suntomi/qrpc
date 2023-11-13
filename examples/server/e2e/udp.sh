#!/bin/bash
CWD=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
# wscat -c ws://localhost:8888/ws
set -eo pipefail

for i in {0..16}; do
  len=$((2 << i))
  echo "send ${len} bytes payload..."
  # TODO: this only uses 64 characters. use all byte value patterns
  randstr=$(openssl rand -base64 ${len} | paste -d' ' -s - | tr -d ' ')
  command=${randstr:0:${len}}
  response=$(echo -n "$command" | nc -uc -w 1 127.0.0.1 9999)
  # echo "cmd=[${command}],resp=[${response}]"
  if [ "$command" != "$response" ]; then
    path=${CWD}/udp-error-${len}.txt
    echo "${command}" > ${path}
    echo "${response}" > ${path}
    echo "Unexpected response: see ${path}"
    exit 1
  fi
done
