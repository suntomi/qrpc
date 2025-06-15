import { QRPCTrack } from './track.js';
export class QRPCMedia {
    path;
    direction;
    initOptions;
    tracks = {};
    nextReconnect = 0;
    lastPing;
    reconnectIntervalMS = null;
    opened = true;
    constructor(path, direction, initOptions) {
        this.path = path;
        this.direction = direction;
        this.initOptions = initOptions || {};
        this.lastPing = new Date().getTime();
        this.keepAlive();
    }
    get isReceiver() {
        return this.direction === "recv";
    }
    keepAlive() {
        this.lastPing = new Date().getTime();
    }
    addTrack(t) {
        this.tracks[t.path] = t;
    }
    removeTrack(t) {
        delete this.tracks[t.path];
    }
    pause(reason) {
        const ret = [];
        for (const k in this.tracks) {
            const reconnectionWaitMS = this.tracks[k].pause(reason);
            if (reason === QRPCTrack.PAUSE_REASON.remote_close) {
                let waitMS = null;
                if (!reconnectionWaitMS && reconnectionWaitMS !== false && reconnectionWaitMS !== null) {
                    waitMS = QRPCTrack.DEFAULT_TRACK_RECONNECTION_WAIT_MS;
                }
                else if (typeof reconnectionWaitMS === "number") {
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
    resume(reason) {
        for (const k in this.tracks) {
            this.tracks[k].resume(reason);
        }
    }
    startReconnect(c, backoffMS) {
        this.reconnectIntervalMS = backoffMS;
        this.nextReconnect = new Date().getTime() + this.reconnectIntervalMS;
        this.reconnect(c);
    }
    stopReconnect() {
        this.reconnectIntervalMS = null;
        this.nextReconnect = 0;
    }
    async reconnect(c) {
        try {
            console.log(`try reconnect for ${this.path}`);
            await c.watchMedia(this.path, {
                onopen: () => { },
                onclose: () => { },
                initOptions: Object.assign(this.initOptions, { sync: true })
            });
        }
        catch (e) {
            console.log(`reconnect failed for ${this.path}`, e);
        }
    }
}
//# sourceMappingURL=media.js.map