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