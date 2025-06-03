# QRPC TypeScript Client

TypeScript client library for QRPC.

## Installation

```bash
npm install @qrpc/client
```

## Usage

```typescript
import { QRPClient, QRPCTrack, QRPCMedia } from '@qrpc/client';

const client = new QRPClient('ws://localhost:8080', 'client-name');
await client.connect();
```

## API Reference

### QRPClient

Main client class for connecting to QRPC server.

### QRPCTrack

Represents a WebRTC track with QRPC-specific functionality.

### QRPCMedia

Represents media stream management.
