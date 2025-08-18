import { QRPCMediaSenderHandler, QRPCMediaReceiverHandler, TrackUpdate, QRPCMidMediaPathMap, Encoding, MayAwaitable } from './types.js';
import { QRPCMedia } from './media.js';
export declare class QRPCTrack {
    static readonly DEFAULT_TRACK_RECONNECTION_WAIT_MS = 5000;
    static readonly PAUSE_REASON: {
        readonly remote_close: "remote_close";
        readonly local_op: "local_op";
        readonly remote_op: "remote_op";
    };
    readonly path: string;
    readonly media: QRPCMedia;
    stream: MediaStream | null;
    track: MediaStreamTrack | null;
    readonly encodings: Encoding[];
    readonly onopen: (track: QRPCTrack) => MayAwaitable<false | null | void>;
    readonly onclose: (track: QRPCTrack) => MayAwaitable<void>;
    readonly onpause?: (track: QRPCTrack, reason: string) => MayAwaitable<number | void | false | null>;
    readonly onresume?: (track: QRPCTrack, reason: string) => MayAwaitable<void>;
    readonly onupdate?: (track: QRPCTrack) => MayAwaitable<void>;
    opened: boolean;
    pausedReasons: string[];
    transceiver: RTCRtpTransceiver | null;
    ssrc?: string;
    static path(canonical_path: string, kind: string): string;
    constructor(path: string, media: QRPCMedia, stream: MediaStream | null, track: MediaStreamTrack | null, encodings: Encoding[], options: QRPCMediaSenderHandler | QRPCMediaReceiverHandler);
    get id(): string;
    get kind(): string;
    get raw(): MediaStreamTrack | null;
    get cname(): string;
    get directory(): string;
    get mid(): string | null;
    get active(): boolean;
    get paused(): boolean;
    get direction(): string;
    get isReceiver(): boolean;
    pausedBy(reason: string): boolean;
    pause(reason: string, noCallback?: boolean): Promise<number>;
    resume(reason: string, noCallback?: boolean): Promise<void>;
    update(update: TrackUpdate): Promise<void>;
    open(pc: RTCPeerConnection, midMediaPathMap?: QRPCMidMediaPathMap): Promise<void>;
    close(pc: RTCPeerConnection, force?: boolean): Promise<void>;
}
//# sourceMappingURL=track.d.ts.map