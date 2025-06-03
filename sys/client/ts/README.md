# QRPC TypeScript Client

TypeScript client library for QRPC.

## Installation

```bash
npm install @qrpc/client
```

## Usage

### ES Modules / TypeScript

```typescript
import { QRPClient, QRPCTrack, QRPCMedia } from '@qrpc/client';

const client = new QRPClient('ws://localhost:8080', 'client-name');
await client.connect();
```

### Browser (Script Tag)

For direct browser usage, use the bundled version:

```html
<script src="node_modules/@qrpc/client/dist/qrpc.bundle.js"></script>
<script>
  const client = new QRPC.QRPClient('http://localhost:8888', 'client-name');
  client.connect();
</script>
```

Or from CDN:
```html
<script src="https://unpkg.com/@qrpc/client/dist/qrpc.bundle.js"></script>
```

## Building

- `npm run build` - Build TypeScript files to individual JS modules
- `npm run build:bundle` - Create minified browser bundle
- `npm run build:all` - Build both module and bundle versions

## API Reference

### QRPClient

Main client class for connecting to QRPC server.

### QRPCTrack

Represents a WebRTC track with QRPC-specific functionality.

### QRPCMedia

Represents media stream management.
