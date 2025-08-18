# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

QRPC is a high-performance RPC framework with real-time media capabilities, primarily focused on WebRTC transport. The project uses C++ for the core implementation with planned bindings for multiple languages.

## Build Commands

### Primary Build System
The project uses Bazel wrapped by Make commands:

```bash
# Complete build (debug mode with address sanitizer - default)
make build

# Release build
make build MODE=release

# Debug build with thread sanitizer
make build MODE=debug SAN=thread

# Debug build without sanitizer (not recommended)
make build MODE=debug SAN=

# Clean build artifacts
make clean
```

### Direct Bazel Commands
```bash
# Debug build with address sanitizer
bazel build :server :client --config=debug --define=SAN=address

# Release build
bazel build :server :client --config=release

# Clean
bazel clean --expunge
```

## Testing

E2E test scripts are located in `sys/tests/e2e/`:

```bash
# WebSocket echo test
./sys/tests/e2e/ws.sh

# WebRTC test (uses Selenium)
./sys/tests/e2e/rtc.sh

# UDP echo test
./sys/tests/e2e/udp.sh

# REST API test
./sys/tests/e2e/rest.sh
```

## Debugging

For debugging with lldb, the project is configured to include debug symbols. Build with:

```bash
# Debug build without sanitizer (recommended for debugging)
make build MODE=debug SAN=address
```

Always try to use address sanitizer even if you directly use bazel command to build for debugging

## Code Architecture

### Directory Structure
- `lib/` - Core C++ server implementation
  - `base/` - Base utilities (webrtc, loop, resolver, logger)
  - `ext/` - External dependencies (mediasoup)
  - `qrpc/` - QRPC-specific implementation
- `sys/client/` - Client implementations
  - `ts/` - TypeScript/JavaScript client
- `sys/tests/` - End-to-end tests
- `bindings/` - Language bindings (planned)
- `cli/` - implementation of proto compiler

### Key Components

1. **Transport Layer**
   - WebRTC data channel implementation and SDP signaling at `lib/base/webrtc/`
   - WebRTC RTP implementation powered by mediasoup at `lib/base/webrtc/rtp`
   - Main WebRTC transport implementation at `lib/qrpc/base/webrtc.h`

2. **Session Management**
   - Session lifecycle in `lib/base/session.h`
   - Stream management for data transfer at `lib/base/session.h`, `lib/qrpc/stream.h`
   - RPC call handling with request/reply pattern `lib/qrpc/stream.h`

3. **Thread Safety**
   - Serial-based object validation system (`lib/qrpc/serial.h`)
   - Thread-safe operations throughout the codebase using moodycamel (`sys/ext/moodycamel`)
   - Event loop integration (`lib/base/loop.h`)

### External Dependencies
- MediaSoup - Built separately via Meson due to Bazel limitations on macOS
- libsdptransform - SDP parsing
- moodycamel - Concurrent queue implementation
- nlohmann/json - JSON handling

### Important Implementation Notes
- The project uses a custom build setup for MediaSoup in `lib/ext/`
- WebRTC transport is the primary focus; WebTransport support is planned
- All public APIs are exposed through `lib/qrpc.h`
- The TypeScript client at `sys/client/ts/client.js` is vanilla JavaScript without build tooling