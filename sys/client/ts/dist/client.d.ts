import type { QRPCMediaSenderParams, QRPCMediaReceiverParams, QRPCStreamParams, QRPCStreamHandler, QRPCMidMediaPathMap, QRPCSyscallArgs, QRPCPromiseCallbacks } from './types.js';
import { QRPCTrack } from './track.js';
import { QRPCMedia } from './media.js';
export declare class QRPClient {
    #private;
    static readonly SYSCALL_STREAM = "$syscall";
    static readonly DEFAULT_SCALABILITY_MODE = "L1T3";
    static readonly NO_INPUT_THRESHOLD = 3;
    static readonly VERBOSE_SYSCALL: string[];
    static readonly MAX_MSGID: number;
    readonly url: string;
    readonly cname: string;
    cert: RTCCertificate | null;
    reconnect: number;
    id: string | null;
    context: any;
    pc: RTCPeerConnection | null;
    syscall_ready: boolean;
    sentTracks: QRPCTrack[];
    midMediaPathMap: QRPCMidMediaPathMap;
    rpcPromises: {
        [msgid: number]: QRPCPromiseCallbacks;
    };
    sdpGen: number;
    msgidSeed: number;
    syscallStream: RTCDataChannel | null;
    timer: ReturnType<typeof setInterval> | null;
    sdpQueue: any[];
    streams: {
        [path: string]: RTCDataChannel;
    };
    medias: {
        [path: string]: QRPCMedia;
    };
    tracks: {
        [path: string]: QRPCTrack;
    };
    iceUsername: string | null;
    icePassword: string | null;
    candidates: RTCIceCandidate[];
    endOfcandidates: boolean;
    onopen?: () => any;
    onclose?: () => number;
    onstream?: (c: RTCDataChannel) => QRPCStreamHandler;
    constructor(url: string, cname?: string);
    initIce(): void;
    get connected(): boolean;
    connect(): Promise<void>;
    parseLocalOffer(localOffer: string): {
        [k: string]: string;
    };
    close(): Promise<void>;
    openMedia(path: string, params: QRPCMediaSenderParams): Promise<QRPCTrack[]>;
    watchMedia(path: string, params: QRPCMediaReceiverParams): Promise<QRPCTrack[]>;
    pauseMedia(path: string): Promise<void>;
    resumeMedia(path: string): Promise<void>;
    updateMedia(path: string, options: {
        stream: MediaStream;
    }): Promise<QRPCTrack[]>;
    closeMedia(path: string): Promise<void>;
    openStream(path: string, params: QRPCStreamParams): RTCDataChannel;
    closeStream(path: string): void;
    watchStream(path: string, options: QRPCStreamHandler): RTCDataChannel;
    syscall(fn: string, args?: QRPCSyscallArgs): Promise<any>;
}
//# sourceMappingURL=client.d.ts.map