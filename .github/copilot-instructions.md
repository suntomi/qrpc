# Project: QRPC - new RPC framwork with High Performance WebRTC Communication Library

## general instruction
- use qrpc for project name, do not follow folder name.
- use english for writing comment on source code, even if instruction is provided by japanese. but chat language should match with instruction one
- do not try to invoke another command if long running command still does not finish. 

## File Structure
- `lib/qrpc.h` - definition of C interface
- `lib/base/` - Core server components
- `lib/qrpc/` - bridge to C interface
- `lib/ext/` - External dependencies
- `tools/bazel/` - Bazel configuration helpers

## Build Commands
- `make ext` - Build external dependencies. use when you change something in mediasoup dependency
- `make sys` - Build main system with Bazel. primarily use this for testing your fix
- `make all` - Build everything
- MODE=debug/release(default: debug), SAN=address/thread/none(default: address)
