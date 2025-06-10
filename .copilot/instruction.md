# Project: QRPC - new RPC framwork with High Performance WebRTC Communication Library

## general instruction
- use qrpc for project name, do not follow folder name.
- use english for writing comment on source code, even if instruction is provided by japanese. but chat language should match with instruction one

## File Structure
- `sys/server/qrpc.h` - definition of C interface
- `sys/server/base/` - Core server components
- `sys/server/qrpc/` - bridge to C interface
- `sys/server/ext/` - External dependencies
- `tools/bazel/` - Bazel configuration helpers

## Build Commands
- `make ext` - Build external dependencies. use when you change something in mediasoup dependency
- `make sys` - Build main system with Bazel. primarily use this for testing your fix
- `make all` - Build everything
- MODE=debug/release(default: debug), SAN=address/thread/none(default: address)
