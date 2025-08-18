import { QRPClient } from './client.js';
import type { QRPCMediaInitOptions } from './types.js';
import { QRPCTrack } from './track.js';
export declare class QRPCMedia {
    readonly path: string;
    readonly direction: string;
    readonly initOptions: QRPCMediaInitOptions;
    readonly tracks: Record<string, QRPCTrack>;
    nextReconnect: number;
    lastPing: number;
    reconnectIntervalMS: number | null;
    opened: boolean;
    constructor(path: string, direction: string, initOptions?: QRPCMediaInitOptions);
    get isReceiver(): boolean;
    keepAlive(): void;
    addTrack(t: QRPCTrack): void;
    removeTrack(t: QRPCTrack): void;
    pause(reason: string): number | undefined;
    resume(reason: string): void;
    startReconnect(c: QRPClient, backoffMS: number): void;
    stopReconnect(): void;
    reconnect(c: QRPClient): Promise<void>;
}
//# sourceMappingURL=media.d.ts.map