import { QRPCTrack } from './track.js';
export type Exclude<T, U> = T extends U ? never : T;
export type MayAwaitable<T> = Exclude<any, Promise<any>> | Promise<T>;
export declare function promisify<T>(a: MayAwaitable<T>): Promise<T>;
export interface QRPCBaseMediaHandler {
    onopen: (track: QRPCTrack) => MayAwaitable<false | null>;
    onclose: (track: QRPCTrack) => MayAwaitable<void>;
    onpause?: (track: QRPCTrack, reason: string) => MayAwaitable<number>;
    onresume?: (track: QRPCTrack, reason: string) => MayAwaitable<void>;
}
export type QRPCMediaSenderHandler = QRPCBaseMediaHandler & {
    onupdate?: (track: QRPCTrack) => MayAwaitable<void>;
};
export type QRPCMediaReceiverHandler = QRPCBaseMediaHandler;
export interface QRPCTrackInitOptions {
    pause?: boolean;
}
export interface QRPCMediaInitOptions {
    sync?: boolean;
    audio?: QRPCTrackInitOptions;
    video?: QRPCTrackInitOptions;
}
export type QRPCMediaSenderParams = QRPCMediaSenderHandler & {
    stream: MediaStream;
    encodings: Encoding[];
    initOptions?: QRPCMediaInitOptions;
};
export type QRPCMediaReceiverParams = QRPCMediaReceiverHandler & {
    initOptions?: QRPCMediaInitOptions;
};
export interface QRPCStreamHandler {
    onopen?: (stream: RTCDataChannel, event: Event) => MayAwaitable<any>;
    onclose?: (stream: RTCDataChannel, event: Event) => MayAwaitable<void>;
    onerror?: (stream: RTCDataChannel, event: Event) => MayAwaitable<void>;
    onmessage: (stream: RTCDataChannel, event: MessageEvent) => MayAwaitable<void>;
}
export type QRPCStreamParams = QRPCStreamHandler & RTCDataChannelInit & {
    publish?: boolean;
};
export interface QRPCSyscallArgs {
    [key: string]: any;
}
export interface QRPCMidMediaPathMap {
    [mid: string]: string;
}
export interface StatusMap {
    [path: string]: {
        pausedReasons?: string[];
    };
}
export interface SyscallMessage {
    fn: string;
    args?: QRPCSyscallArgs;
    msgid?: number;
}
export interface QRPCPromiseCallbacks {
    resolve: (value?: any) => void;
    reject: (reason?: any) => void;
}
export interface TrackUpdate {
    stream: MediaStream;
    track: MediaStreamTrack;
}
export interface Encoding {
    rid?: string;
    scalabilityMode?: string;
    maxBitrate?: number;
    [key: string]: any;
}
//# sourceMappingURL=types.d.ts.map