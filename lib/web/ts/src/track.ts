import {
  QRPCMediaSenderHandler, QRPCMediaReceiverHandler,
  TrackUpdate, QRPCMidMediaPathMap, Encoding, MayAwaitable, promisify
} from './types.js';
import { QRPCMedia } from './media.js';

export class QRPCTrack {
  static readonly DEFAULT_TRACK_RECONNECTION_WAIT_MS = 5000;
  static readonly PAUSE_REASON = {
    remote_close: "remote_close",
    local_op: "local_op",
    remote_op: "remote_op",
  } as const;

  public readonly path: string;
  public readonly media: QRPCMedia;
  public stream: MediaStream | null;
  public track: MediaStreamTrack | null;
  public readonly encodings: Encoding[];
  public readonly onopen: (track: QRPCTrack) => MayAwaitable<false | null | void>;
  public readonly onclose: (track: QRPCTrack) => MayAwaitable<void>;
  public readonly onpause?: (track: QRPCTrack, reason: string) => MayAwaitable<number | void | false | null>;
  public readonly onresume?: (track: QRPCTrack, reason: string) => MayAwaitable<void>;
  public readonly onupdate?: (track: QRPCTrack) => MayAwaitable<void>;
  public opened: boolean = false;
  public pausedReasons: string[] = [];
  public transceiver: RTCRtpTransceiver | null = null;
  public ssrc?: string;

  static path(canonical_path: string, kind: string): string {
    return canonical_path + kind;
  }

  constructor(
    path: string,
    media: QRPCMedia,
    stream: MediaStream | null,
    track: MediaStreamTrack | null,
    encodings: Encoding[],
    options: QRPCMediaSenderHandler | QRPCMediaReceiverHandler
  ) {
    this.path = path;
    this.media = media;
    this.media.addTrack(this);
    this.stream = stream;
    this.track = track;
    this.encodings = encodings;
    this.onopen = options.onopen;
    this.onclose = options.onclose;
    this.onpause = options.onpause || (() => {});
    this.onresume = options.onresume || (() => {});
    this.onupdate = (options as QRPCMediaSenderHandler).onupdate || (() => {});
  }

  get id(): string {
    return this.track?.id || '';
  }

  get kind(): string {
    const parsed = this.path.split("/");
    return parsed[parsed.length - 1];
  }

  get raw(): MediaStreamTrack | null {
    return this.track;
  }

  get cname(): string {
    const parsed = this.path.split("/");
    if (!this.isReceiver) {
      throw new Error("cname is only for receiver track");
    }
    return parsed[0];
  }

  get directory(): string {
    return this.path.split("/").slice(0, -1).join("/");
  }

  get mid(): string | null {
    return this.transceiver?.mid || null;
  }

  get active(): boolean {
    return this.track != null;
  }

  get paused(): boolean {
    return this.pausedReasons.length > 0;
  }

  get direction(): string {
    return this.media.direction;
  }

  get isReceiver(): boolean {
    return this.media.isReceiver;
  }

  pausedBy(reason: string): boolean {
    return this.pausedReasons.indexOf(reason) >= 0;
  }

  pause(reason: string, noCallback?: boolean): Promise<number|undefined> {
    const i = this.pausedReasons.indexOf(reason);
    if (i < 0) {
      this.pausedReasons.push(reason);
    }
    return promisify(!noCallback && this.onpause && this.onpause(this, reason));
  }

  resume(reason: string, noCallback?: boolean): Promise<void> {
    const i = this.pausedReasons.indexOf(reason);
    if (i >= 0) {
      this.pausedReasons.splice(i, 1);
      return promisify(!noCallback && this.onresume && this.onresume(this, reason));
    }
    return Promise.resolve();
  }

  update(update: TrackUpdate): Promise<void> {
    if (!this.transceiver) {
      throw new Error("track is not started");
    }
    this.stream = update.stream;
    this.transceiver.sender.replaceTrack(update.track);
    this.track?.stop();
    this.track = update.track;
    return promisify(this.onupdate?.(this));
  }

  async open(pc: RTCPeerConnection, midMediaPathMap?: QRPCMidMediaPathMap): Promise<void> {
    if (this.direction !== "send" || this.track == null) {
      throw new Error("open is only needed for send tracks");
    }

    if (midMediaPathMap) {
      // want to put tracks to actual peer connection (not for generating localOffer for producing)
      let transceiver: RTCRtpTransceiver | undefined;
      
      // find transceiver for this track by comparing logical path of the track and path decided by server mid
      for (const t of pc.getTransceivers()) {
        if (!t.sender) {
          console.log("ignore receiver transceiver", t.sender);
          continue;
        }
        // in here, pc.setRemoteDescription is already called, so mid is decided by server remote offer
        // also midMediaPathMap is updated by syscall ("produce") or whip API call (in QRPClient.#handshake)
        const path = midMediaPathMap[t.mid!];
        if (!path) {
          console.log("no path for mid:", t.mid, midMediaPathMap);
          continue;
        }
        if (path == this.path) {
          transceiver = t;
          break;
        }
      }
      
      if (!transceiver) {
        throw new Error("no correspond transceiver:" + this.path);
      }
      
      console.log("found transceiver for", this.path, transceiver);
      transceiver.direction = "sendonly";
      
      if (this.kind === "video") {
        const params = transceiver.sender.getParameters();
        await transceiver.sender.setParameters(Object.assign(params, { encodings: this.encodings }));
      }
      
      await transceiver.sender.replaceTrack(this.track);
      this.transceiver = transceiver;
    } else {
      if (this.track.kind === "video") {
        this.transceiver = pc.addTransceiver(
          this.track,
          { direction: 'sendonly', sendEncodings: this.encodings, streams: [this.stream!] }
        );
      } else if (this.track.kind === "audio") {
        this.transceiver = pc.addTransceiver(
          this.track,
          { direction: 'sendonly', streams: [this.stream!] }
        );
      } else {
        throw new Error(`invalid track kind ${this.track.kind}`);
      }
    }
  }

  async close(pc: RTCPeerConnection, force?: boolean): Promise<void> {
    if (this.track) {
      this.onclose?.(this);
      
      // once track is stopped, it cannot be reused for receiver at least in chrome browser.
      // for unknown reason (might be bug), 
      // if track is stopped, ontrack event of corresponding SDP media section always contains old 'readyState=ended' track.
      // even if chrome statistics shows that track is active again (even with different ssrc)
      // the reason I think this might be bug, 
      // is that track id does not change regardless SDP sends different msid for corresponding media section.
      // for workaround, we do not stop track for receiver.
      // QRPClient.close call the function with force = true, so that we can stop track for receiver for cleanup case.
      if (force || !this.isReceiver) {
        if (this.transceiver?.mid) {
          pc.removeTrack(this.transceiver.sender);
        }
        this.track.stop();
      }
      
      this.track = null;
      this.transceiver = null;
      this.stream = null;
      this.opened = false;
      this.pausedReasons = [];
    }
    this.media.removeTrack(this);
  }
}
