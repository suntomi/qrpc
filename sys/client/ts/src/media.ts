import { QRPClient } from './client.js';
import type { QRPCMediaInitOptions } from './types.js';
import { QRPCTrack } from './track.js';

export class QRPCMedia {
  public readonly path: string;
  public readonly direction: string;
  public readonly initOptions: QRPCMediaInitOptions;
  public readonly tracks: Record<string, QRPCTrack> = {};
  public nextReconnect: number = 0;
  public lastPing: number;
  public reconnectIntervalMS: number | null = null;
  public opened: boolean = true;

  constructor(path: string, direction: string, initOptions?: QRPCMediaInitOptions) {
    this.path = path;
    this.direction = direction;
    this.initOptions = initOptions || {};
    this.lastPing = new Date().getTime();
    this.keepAlive();
  }

  get isReceiver(): boolean {
    return this.direction === "recv";
  }

  keepAlive(): void {
    this.lastPing = new Date().getTime();
  }

  addTrack(t: QRPCTrack): void {
    this.tracks[t.path] = t;
  }

  removeTrack(t: QRPCTrack): void {
    delete this.tracks[t.path];
  }

  async pause(reason: string): Promise<number | undefined> {
    const ret: number[] = [];
    for (const k in this.tracks) {
      const reconnectionWaitMS = await this.tracks[k].pause(reason);
      if (reason === QRPCTrack.PAUSE_REASON.remote_close) {
        let waitMS: number | null = null;
        if (!reconnectionWaitMS) {
          waitMS = QRPCTrack.DEFAULT_TRACK_RECONNECTION_WAIT_MS;
        } else if (typeof reconnectionWaitMS === "number") {
          waitMS = reconnectionWaitMS;
        }
        if (waitMS) {
          ret.push(waitMS);
        }
      }
    }
    if (ret.length > 0) {
      // sort by ascending order and return first element (that is, minimum element)
      return ret.sort()[0];
    }
  }

  resume(reason: string): void {
    for (const k in this.tracks) {
      this.tracks[k].resume(reason);
    }
  }

  startReconnect(c: QRPClient, backoffMS: number): void {
    this.reconnectIntervalMS = backoffMS;
    this.nextReconnect = new Date().getTime() + this.reconnectIntervalMS;
    this.reconnect(c);
  }

  stopReconnect(): void {
    this.reconnectIntervalMS = null;
    this.nextReconnect = 0;
  }

  async reconnect(c: QRPClient): Promise<void> {
    try {
      console.log(`try reconnect for ${this.path}`);
      await c.watchMedia(this.path, {
        onopen: () => {},
        onclose: () => {},
        initOptions: Object.assign(this.initOptions, { sync: true })
      });
    } catch (e) {
      console.log(`reconnect failed for ${this.path}`, e);
    }
  }
}
