import type {
  QRPCMediaSenderParams, QRPCMediaReceiverParams, QRPCMediaInitOptions,
  QRPCStreamParams, QRPCStreamHandler, 
  QRPCMidMediaPathMap, QRPCSyscallArgs, QRPCPromiseCallbacks
} from './types.js';
import { promisify } from './types.js';
import { QRPCTrack } from './track.js';
import { QRPCMedia } from './media.js';

export class QRPClient {
  static readonly SYSCALL_STREAM = "$syscall";
  static readonly DEFAULT_SCALABILITY_MODE = "L1T3";
  static readonly NO_INPUT_THRESHOLD = 3;
  static readonly VERBOSE_SYSCALL = ["ping", "ping_ack"];
  static readonly MAX_MSGID = Number.MAX_SAFE_INTEGER;

  public readonly url: string;
  public readonly cname: string;
  public cert: RTCCertificate | null = null;
  public reconnect: number = 0;
  public id: string | null = null;
  public context: any = null;
  public pc: RTCPeerConnection | null = null;
  public syscall_ready: boolean = false;
  public sentTracks: QRPCTrack[] = [];
  public midMediaPathMap: QRPCMidMediaPathMap = {};
  public rpcPromises: { [msgid: number]: QRPCPromiseCallbacks } = {};
  public sdpGen: number = 0;
  public msgidSeed: number = 1;
  public syscallStream: RTCDataChannel | null = null;
  public timer: ReturnType<typeof setInterval> | null = null;
  public sdpQueue: any[] = [];
  public streams: { [path: string]: RTCDataChannel } = {};
  public medias: { [path: string]: QRPCMedia } = {};
  public tracks: { [path: string]: QRPCTrack } = {};
  public iceUsername: string | null = null;
  public icePassword: string | null = null;
  public candidates: RTCIceCandidate[] = [];
  public endOfcandidates: boolean = false;
  public onopen?: () => any;
  public onclose?: () => number;
  public onstream?: (c: RTCDataChannel) => QRPCStreamHandler;

  constructor(url: string, cname?: string) {
    this.url = url;
    this.cname = cname || this.#genCN();
    console.log("QRPClient", this.cname, url);
    this.#clear();
  }

  async #syscallMessageHandler(s: RTCDataChannel, event: MessageEvent): Promise<void> {
    const data = JSON.parse(event.data);
    if (QRPClient.VERBOSE_SYSCALL.indexOf(data.fn) < 0) {
      console.log("syscall", data);
    }
    if (!data.msgid) {
      if (data.fn === "close") {
        console.log("shutdown by server");
        this.#close();
      } else if (data.fn === "close_track") {
        console.log("close_track", data.args.path);
        await this.#closeTracks([data.args.path]);
      } else if (data.fn == "remote_pause" || data.fn == "remote_resume") {
        const t = this.tracks[data.args.path];
        if (t) {
          if (data.fn == "remote_pause") {
            t.pause(QRPCTrack.PAUSE_REASON.remote_op);
          } else {
            t.resume(QRPCTrack.PAUSE_REASON.remote_op);
          }
        } else {
          throw new Error(`no such track for ${data.fn}: ${data.args.path}`);
        }
      } else if (data.fn == "ping") {
        if (data.args.path) {
          const t = this.tracks[data.args.path];
          if (t && t.media) {
            t.media.keepAlive();
          }
        }
      } else {
        throw new Error("unknown syscall: " + JSON.stringify(data));
      }
      return;
    }
    const promise = this.#fetchPromise(data.msgid);
    if (!promise) {
      console.log("no promise for msgid", data.msgid);
      return;
    }
    if (data.args && data.args.error) {
      promise.reject(new Error(data.args.error));
      return;
    }
    if (data.fn == "consume") {
      throw new Error("currently, publish to other peer is not suported");
    } else if (data.fn == "consume_ack") {
      if (!data.args.sdp) {
        promise.reject(new Error(`invalid response: no sdp: ${JSON.stringify(data.args)}`));
        return;
      }
      for (const k in data.args.status_map || {}) {
        const st = data.args.status_map[k];
        const t = this.tracks[k];
        if (t) {
          for (const r of st.pausedReasons || []) {
            t.pause(r, true);
          }
        } else {
          console.log(`no such track for [${k}]`, this.tracks, this.tracks[k]);
        }
      }
      Object.assign(this.midMediaPathMap, data.args.mid_media_path_map || {});
      console.log("midMediaPathMap => ", this.midMediaPathMap);
      promise.resolve(data.args.sdp);
    } else if (data.fn == "produce") {
      throw new Error("currently, publish to other peer is not suported");
    } else if (data.fn == "produce_ack") {
      if (!data.args.sdp) {
        promise.reject(new Error(`invalid response: no sdp: ${JSON.stringify(data.args)}`));
        return;
      }
      Object.assign(this.midMediaPathMap, data.args.mid_media_path_map || {});
      console.log("midMediaPathMap => ", this.midMediaPathMap);
      for (const k in data.args.status_map || {}) {
        const st = data.args.status_map[k];
        const t = this.tracks[k];
        if (t) {
          for (const r of st.pausedReasons || []) {
            t.pause(r, true);
          }
        }
      }
      promise.resolve(data.args.sdp);
    } else if (data.fn == "close_media_ack") {
      await this.#closeTracks(data.args.paths);
      promise.resolve(data.args.sdp);
    } else if (
      data.fn == "resume_ack" || data.fn == "pause_ack" || data.fn == "close_ack" ||
      data.fn == "sync_ack" || data.fn == "ping_ack" || data.fn == "publish_stream_ack" ||
      data.fn == "remote_answer_ack"
    ) {
      promise.resolve();
    } else {
      console.log("unknown syscall", data);
    }
  }
  #genCN(): string {
    const bytes = new Uint8Array(8);
    crypto.getRandomValues(bytes);
    return btoa(String.fromCharCode(...bytes))
      // make result url safe
      .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  }
  #clear(): void {
    this.streams = {};
    this.medias = {};
    this.tracks = {};
    this.syscall_ready = false;
    // we use array to keep sent track add order.
    // it is important to match them with SDP media section order
    this.sentTracks = []; 
    this.midMediaPathMap = {};
    this.rpcPromises = {};
    this.sdpGen = 0;
    this.msgidSeed = 1;
    this.id = null;
    this.syscallStream = null;
    this.timer = null;
    this.sdpQueue = [];
  }

  initIce(): void {
    //Ice properties
    this.iceUsername = null;
    this.icePassword = null;
    //Pending candidadtes
    this.candidates = [];
    this.endOfcandidates = false;
  }

  #newMsgId(): number {
    if (this.msgidSeed >= QRPClient.MAX_MSGID) {
      this.msgidSeed = 1;
    }
    return this.msgidSeed++;
  }

  #fetchPromise(msgid: number): QRPCPromiseCallbacks | null {
    const p = this.rpcPromises[msgid];
    if (p) {
      delete this.rpcPromises[msgid];
    }
    return p;
  }

  #handshaked(): boolean {
    return this.syscall_ready && this.pc !== null && this.pc.connectionState !== "failed";
  }

  #incSdpGen(): number {
    return ++this.sdpGen;
  }

  get connected() {
    return this.pc?.connectionState === "connected";
  }

  async connect(): Promise<void> {
    //If already publishing
    if (this.pc) {
      console.log("Already connected");
      return;
    }
    // create dummy peer connection to generate sdp
    const pc = await this.#createPeerConnection();
    // Store pc object and token
    this.pc = pc;
    this.initIce();
    this.#setupCallbacks(pc);
    await this.#handshake();
  }

  async #createPeerConnection(): Promise<RTCPeerConnection> {
    if (!this.cert) {
      this.cert = await RTCPeerConnection.generateCertificate({
        name: 'ECDSA',
        namedCurve: 'P-256'
      } as EcKeyGenParams);
    }
    // always uses same cert for peer connection
    return new RTCPeerConnection({
      iceServers: [],
      certificates: [this.cert]
    });
  }

  #setupCallbacks(pc: RTCPeerConnection): void {
    // Listen for data channels
    pc.ondatachannel = (event) => {       
      const s = event.channel;
      const path = s.label;
      console.log(`accept stream ${path}`);
      if (!this.onstream) {
        s.close();
        throw new Error("QRPClient.onstream is mandatory");
      }
      const h = this.onstream(s);
      if (!h) {
        console.log(`No stream callbacks for path [${path}]`);
        s.close();
        return;
      }
      this.#setupStream(s, h);
    }

    // Listen addition of media tracks
    pc.ontrack = async (event) => {
      console.log("ontrack", event);
      const track = event.track;
      const tid = track.id;
      const receiver = event.receiver;
      let path = undefined;
      if (event.transceiver) {
        if (event.transceiver.mid === "probator") { return; }
        if (!event.transceiver.mid) {
          throw new Error(`transceiver has no mid: ${event.transceiver}`);
        }
        path = this.midMediaPathMap[event.transceiver.mid];
        if (!path) {
          throw new Error(`No path is defined for mid = ${event.transceiver.mid}`);
        }
      } else {
        throw new Error("event has no transceiver");
      }
      let t = this.tracks[path];
      if (!t) {
        console.log(`No media for path ${path}`);
        track.stop();
        return;
      }
      if (t.direction === "recv") {
        t.track = track;
        t.transceiver = event.transceiver;
        t.stream = event.streams[0];
      }
      if (!t.opened) {
        if (t.onopen) {
          const r = await Promise.resolve(t.onopen(t));
          if (r === false || r === null) {
            console.log(`close media by application ${path}`);
            this.closeMedia(path);
            return;
          }
        }
        t.opened = true;
      }
    }
    
    // Listen for state change events
    pc.oniceconnectionstatechange = (event) => {
      console.log("ICE connection state change", pc.iceConnectionState);
    }
    pc.onconnectionstatechange = async (event) =>{
      console.log("Connection state change", pc.connectionState);
      switch(pc.connectionState) {
        case "connected":
          // The connection has become fully connected
          break;
        case "disconnected":
        case "failed":
          // One or more transports has terminated unexpectedly or in an error
          await this.#close();
          break;
        case "closed":
          // The connection has been closed
          break;
      }
    }

    // Listen for candidates
    pc.onicecandidate = (event) => {
      if (event.candidate) {
        // Ignore candidates not from the first m line
        if ((event.candidate.sdpMLineIndex || 0) >0) {
          return;
        }
        this.candidates.push(event.candidate);                                         
      } else {
        // No more candidates
        this.endOfcandidates = true;
      }
    }
    this.timer = setInterval(async () => {
      const now = (new Date()).getTime();
      await this.#checkMedias(now);
    }, 1000); // 1秒ごとにチェック
  }

  parseLocalOffer(localOffer: string) {
    const result: { [mid: string]: { [ssrc: string]: string[] } } = {};
    let currentMid: string | undefined = undefined;
    let currentSsrcs : { [ssrc: string]: string[] } = {};
    const lines = localOffer.split(/\r?\n/);
    lines.push("m=dummy"); // ensure last section processed
    for (const l of lines) {
      if (l.startsWith("m=")) {
        currentMid = undefined;
        currentSsrcs = {};
      } else if (l.startsWith("a=mid:")) {
        if (!currentMid) {
          // a=mid:$mid_value
          currentMid = l.slice(6).trim();
          result[currentMid] = currentSsrcs;
        } else {
          throw new Error(`invalid find a=mid line twice before reset by m= line ${currentMid},${l.slice(6)}`);
        }
      } else if (l.startsWith("a=ssrc:")) {
        //a=ssrc:$ssrc_value $attribute_name:$attribute_value
        const parsed = l.slice(7).split(/\s/,2);
        const ssrc = parsed[0].trim();
        const attrName = parsed[1].split(/:/, 1)[0].trim();
        if (!currentSsrcs[ssrc]) {
          currentSsrcs[ssrc] = [];
        }
        currentSsrcs[ssrc].push(attrName);
        if (currentMid) {
          result[currentMid] = currentSsrcs;
        }
      }
    }
    // result will be like { [mid]: { [ssrc]: [attr1, attr2, ...] } }
    // filter element that has multiple ssrcs (dedupe lines with same ssrc), probably simulcast media section of firefox
    return Object.fromEntries(
      Object.entries(result).filter(v => Object.keys(v[1]).length == 1).map(v => [v[0], Object.keys(v[1])[0]])
    );
  }

  async #checkMedias(now: number): Promise<void> {
    let mediaOpened = false;
    for (const k in this.medias) {
      const m = this.medias[k];
      if (m.isReceiver) {
        // console.log("check track", t.path, now - t.lastPing);
        if (now - m.lastPing > (QRPClient.NO_INPUT_THRESHOLD * 1000)) {
          if (m.opened) {
            m.opened = false;
            const reconnectionWaitMS = await m.pause(QRPCTrack.PAUSE_REASON.remote_close);
            console.log(`no ping for ${m.path} for ${QRPClient.NO_INPUT_THRESHOLD * 1000} ms`, reconnectionWaitMS);
            if (reconnectionWaitMS) {
              m.startReconnect(this, reconnectionWaitMS);
              console.log(`track ${m.path} will try reconnect every ${reconnectionWaitMS} ms`);
            }
          } else if (m.reconnectIntervalMS && now > m.nextReconnect) {
            m.reconnect(this);
            m.nextReconnect = now + m.reconnectIntervalMS;
          }
        } else if (!m.opened) {
          console.log(`input again for ${m.path}`);
          m.opened = true;
          m.stopReconnect();
          m.resume(QRPCTrack.PAUSE_REASON.remote_close);
        }
      } else if (m.opened) {
        mediaOpened = true;
      }
    }
    if (mediaOpened) {
      this.syscall("ping", {});
    }
  }

  async #createOffer(tracks: QRPCTrack[]): Promise<{
    localOffer: RTCSessionDescriptionInit, midPathMap: { [mid: string]: string }
  }> {
    const midPathMap: { [mid: string]: string } = {};
    // create dummy peer connection to generate sdp
    const pc = await this.#createPeerConnection();
    // emurate creating stream to generate correct sdp
    pc.createDataChannel("dummy");
    for (const t of tracks) {
      await t.open(pc);
    }
    const localOffer = await pc.createOffer();
    await pc.setLocalDescription(localOffer);
    const midSsrcMap = this.parseLocalOffer(localOffer.sdp!);
    // console.log("midSsrcMap", midSsrcMap);
    for (const t of tracks) {
      // now, mid is decided by calling pc.setLocalDescription. 
      // mid (in server-responded remote offer) probably changes 
      // after it processes on server side, but because of client mid actually decided
      // by server remote offer, changes causes no problem.
      midPathMap[t.mid!] = t.path;
      t.ssrc = midSsrcMap[t.mid!] || undefined;
      // console.log(`${t.path},mid=${t.mid} ssrc = ${t.ssrc}`);
    }
    pc.close();
    return {localOffer, midPathMap};
  }

  #fixupLocalAnswer(localAnswer: string, midSsrcMap: { [mid: string]: string }): string {
    // console.log("#fixupLocalAnswer", midSsrcMap);
    const chunks = [];
    let chunk: string[] = [];
    const lines = localAnswer.split(/\r?\n/);
    lines.push("m=dummy"); // ensure last section processed
    let currentMid;
    for (const l of lines) {
      if (l.startsWith("m=")) {
        if (!currentMid) {
          if (chunks.length > 0) {
            throw new Error(`only first chunk allowed without mid`);
          }
          chunks.push({mid: null, chunk});
        } else {
          if (chunk.length <= 0) {
            throw new Error(`should have chunk for ${currentMid}`);
          }
          chunks.push({mid: currentMid, chunk});
        }
        chunk = [l];
        currentMid = undefined;
        continue;
      }
      chunk.push(l);
      if (l.startsWith("a=mid:")) {
        if (!currentMid) {
          // a=mid:$mid_value
          currentMid = l.slice(6);
        } else {
          throw new Error(`invalid find a=mid line twice before reset by m= line ${currentMid},${l}`);
        }
      }
    }
    const sdp = [];
    for (const c of chunks) {
      const ssrc = c.mid !== null ? midSsrcMap[c.mid] : null;
      if (ssrc) {
        const text = c.chunk.join("\n");
        sdp.push(text.replace(/a=ssrc:[0-9]+/g, `a=ssrc:${ssrc}`));
      } else {
        sdp.push(c.chunk.join("\n"));
      }
    }
    return sdp.join("\n");
  }

  async #setRemoteOffer(remoteOffer: string, sdpGen: number, sentTracks: QRPCTrack[]): Promise<void> {
    console.log("remote offer sdp", remoteOffer, sdpGen);
    if (this.pc == null) {
      throw new Error("peer connection is not initialized");
    }
    // if there is sdp which generation is later than current sdpGen, the generation is skipped
    for (const e of this.sdpQueue) {  
      if (e.sdpGen > sdpGen) {
        console.log(`skip sdpGen=${sdpGen} because newer generation exists: ${e.sdpGen}`);
        return;
      }
    }
    this.sdpQueue.push({remoteOffer, sdpGen, sentTracks});
    if (this.sdpQueue.length > 1) {
      console.log(`queue sdpGen=${sdpGen}`, this.sdpQueue);
      return;
    }
    // set remote description
    await this.pc.setRemoteDescription({type:"offer",sdp:remoteOffer});
    // set tracks to actual peer connection
    const midSsrcMap: { [mid: string]: string } = {};
    for (const t of sentTracks) {
      await t.open(this.pc, this.midMediaPathMap);
      if (t.ssrc) {
        midSsrcMap[t.mid!] = t.ssrc;
      }
    }
    const answer = await this.pc.createAnswer();
    // answer.sdp = this.#fixupLocalAnswer(answer.sdp, midSsrcMap);
    // console.log("local answer sdp", answer.sdp);
    if (this.#handshaked()) {
      const newMidSsrcMap = this.parseLocalOffer(answer.sdp!);
      console.log("midSsrcMaps", midSsrcMap, newMidSsrcMap);
      await this.syscall("remote_answer", {
        midMap: Object.fromEntries(
          Object.keys(midSsrcMap).map(k => [k, {
            ssrc_fixups:[[Number(midSsrcMap[k]),Number(newMidSsrcMap[k])]]
          }])
        ),
      });
      // update ssrc of tracks
      for (const t of sentTracks) {
        t.ssrc = newMidSsrcMap[t.mid!] || undefined;
      }
    }
    await this.pc.setLocalDescription(answer);
    console.log(`apply sdpGen=${sdpGen} is finished`);
    if (this.sdpQueue.length > 1) {
      // async sleep to stabilize frequent sdp update
      await new Promise(r => setTimeout(r, 1000)); // this may add more sdp to the queue
      // fetch last element of the queue and execute.
      const e = this.sdpQueue[this.sdpQueue.length - 1];
      // by above filtering, elements in the queue has sdpGen value with ascending order.
      this.sdpQueue = []; // clear queue (because non-last element should be outdated and can ignore)
      // so that e should never be queued again.
      await this.#setRemoteOffer(e.remoteOffer, e.sdpGen, e.sentTracks); // during this call, sdpQueue may be filled again
    } else {
      this.sdpQueue = [];
    }
  }

  async #capability() {
    const pc = new RTCPeerConnection();
    pc.addTransceiver("audio");
    pc.addTransceiver("video");
    const offer = await pc.createOffer({offerToReceiveAudio: true, offerToReceiveVideo: true});
    // console.log("capability sdp", offer.sdp);
    return offer.sdp;
  }
  async #handshake() {
    if (this.#handshaked()) {
      throw new Error("handshake only called once in a session");
    }
    // generate syscall stream (it also ensures that SDP for data channel is generated)
    const syscallReady = new Promise((resolve, reject) => {
      this.syscallStream = this.openStream(QRPClient.SYSCALL_STREAM, {
        onmessage: this.#syscallMessageHandler.bind(this),
        onopen: (s, e) => resolve(e), onerror: (s, e) => reject(e)
      });
    });
    const sdpGen = this.#incSdpGen();
    const sentTracks = [...this.sentTracks];
    // Create new SDP offer without initializing actual peer connection
    const {localOffer} = await this.#createOffer(sentTracks);
    console.log("local offer sdp", localOffer.sdp);

    //store local ice ufrag/pwd
    this.iceUsername = localOffer.sdp!.match(/a=ice-ufrag:(.*)[\r\n]+/)![1];
    this.icePassword = localOffer.sdp!.match(/a=ice-pwd:(.*)[\r\n]+/)![1];

    //Do the post request to the WHIP endpoint with the SDP offer
    const fetched = await fetch(this.url, {
      method: "POST",
      body: JSON.stringify({
        sdp:localOffer.sdp,
        cname:this.cname,
        capability: await this.#capability()
      }),
      headers: {
        "Content-Type": "application/json"
      }
    });
    if (!fetched.ok) {
      throw new Error(`Request rejected with status ${fetched.status}`);
    }

    //Get the SDP answer
    let text = undefined;
    try {
      text = await fetched.text();
      const {sdp: remoteOffer, mid_media_path_map: midMediaPathMap} = JSON.parse(text);
      for (const k in midMediaPathMap || {}) {
        this.midMediaPathMap[k] = midMediaPathMap[k];
      }
      console.log("midMediaPathMap =>", this.midMediaPathMap);

      this.id = remoteOffer.match(/a=ice-ufrag:(.*)[\r\n]+/)[1];
      console.log("id", this.id);

      await this.#setRemoteOffer(remoteOffer, sdpGen, sentTracks);
      await syscallReady;
      this.syscall_ready = true;
      if (this.onopen) {
        this.context = await this.onopen();
      }  
    } catch (e) {
      console.log(`error in handling whip response: ${e}|${text || "no response"}`);
      throw e;
    }
  }
  async close(): Promise<void> {
    await this.#close(true);
  }

  async #close(fromLocal?: boolean): Promise<void> {
    if (!this.pc) {
      // Already stopped
      return
    }
    if (fromLocal) {
      // send close notify to server
      await this.syscall("close", {});
    }
    let reconnectionWaitMS;
    if (this.onclose) {
      reconnectionWaitMS = this.onclose();
      if (!reconnectionWaitMS) {
        reconnectionWaitMS = 0; 
      } else {
        // nsec to msec
        reconnectionWaitMS = reconnectionWaitMS / 1000 / 1000;
      }
    } else {
      // default 5 sec (TODO: configurable)
      reconnectionWaitMS = 5000;
    }
    for (const k in this.tracks) {
      console.log("close track", k); 
      await this.tracks[k].close(this.pc, true); // trur for closing receivers
    }
    for (const k in this.streams) {
      console.log("close stream", k);
      this.streams[k].close();
    }
    if (this.timer) {
      clearInterval(this.timer);
    }
    if (this.pc.connectionState != "failed") {
      this.pc.close();
    }
    this.pc = null;
    this.#clear();
    if (reconnectionWaitMS > 0) {
      console.log(`attempt reconnect after ${reconnectionWaitMS} ms`);
      setTimeout(() => {
        this.reconnect++;
        this.connect();
      }, reconnectionWaitMS);
    } else {
      console.log("no reconnect. bye!");
    }
  }

  #canonicalOpenPath(path: string): string {
    const parsed = path.split('/');
    if (parsed.length == 1) {
      return path + "/";
    } else {
      if (parsed[parsed.length - 1].length > 0) {
        throw new Error(`invalid path: ${path}: should be ended with /`);
      }
      return path;
    }
  }

  async openMedia(path: string, params: QRPCMediaSenderParams): Promise<QRPCTrack[]> {
    const {stream, encodings, onopen, onclose, onupdate, onpause, onresume, initOptions} = params;
    const cpath = this.#canonicalOpenPath(path);
    console.log("openMedia", cpath, stream, encodings);
    if (!encodings) { throw new Error("encodings is mandatory"); }
    if (encodings.length > 3) { throw new Error("encodings more than 3 may not be treated correctly"); }
    const ridScalabilityModeMap: {[rid: string]: string} = {};
    let index = 0;
    encodings.forEach(e => {
      if (!e.maxBitrate) { throw new Error("for each encodings, maxBitrate is mandatory"); }
      if (e.rid) { throw new Error(`cannot specify rid for encodings`); }
      e.rid = `r${index++}`;
      e.scalabilityMode = e.scalabilityMode || QRPClient.DEFAULT_SCALABILITY_MODE;
      ridScalabilityModeMap[e.rid] = e.scalabilityMode;
    });
    // sort by maxBitrate asc, because server regards earlier encoding as lower quality,
    // regardless its actual bitrate
    encodings.sort((a, b) => a.maxBitrate! - b.maxBitrate!);
    const tracks: QRPCTrack[] = [];
    stream.getTracks().forEach(t => {
      const path = QRPCTrack.path(cpath, t.kind);
      const media = this.#addMedia(cpath, "send", initOptions);
      const track = new QRPCTrack(path, media, stream, t, encodings, {
        onopen, onclose, onupdate, onpause, onresume
      });
      // console.log("openMedia: add track for", track.path);
      this.tracks[track.path] = track;
      this.sentTracks.push(track);
      tracks.push(track);
    });
    if (this.#handshaked()) {
      // already handshaked, so renegotiate for newly produced tracks.
      const {localOffer, midPathMap} = await this.#createOffer(tracks);
      console.log("openMedia: local offer", localOffer.sdp, midPathMap);
      const sdpGen = this.#incSdpGen();
      const sentTracks = [...this.sentTracks];
      try {
        const remoteOffer = await this.syscall("produce", {
          sdp: localOffer.sdp, initOptions, midPathMap, rtp: {ridScalabilityModeMap}
        });
        await this.#setRemoteOffer(remoteOffer, sdpGen, sentTracks);
      } catch (e) {
        console.log(`openMedia: remote negotiation failed: ${e}`);
        for (const t of tracks) {
          delete this.medias[t.media.path];
          delete this.tracks[t.path];
          await t.close(this.pc!);
        }
        throw e;
      }
    }
    return tracks;
  }

  #addMedia(path: string, direction: string, initOptions?: QRPCMediaInitOptions): QRPCMedia {
    // 1. path/(video|audio) => path/
    // 2. path/ => path/
    const parsed = path.split("/");
    let media;
    if (parsed[parsed.length - 1] === "video" || parsed[parsed.length - 1] === "audio") {
      path = path.slice(0, -5);
    }
    if (this.medias[path]) {
      media = this.medias[path];
    } else {
      media = new QRPCMedia(path, direction, initOptions);
      this.medias[path] = media;
    }
    return media;
  }

  // media_path is one of the following:
  // 1. ${cname}/${local_path}/ => all media kind under local_path consumed
  // 2. ${cname}/${local_path}/${media_kind} => only media_kind under local_path consumed
  // for 1. last / is mandatory to indicate that it is a directory.
  // but / can be omitted if last component of local_path does not ssem to be a media kind (not audio/video)
  #canonicalViewPath(path: string): {cpath: string, kind: string | undefined} {
    const parsed = path.split('/');
    if (parsed.length < 2) {
      throw new Error(`invalid path: ${path}: at least \${cname}/\${single_component_local_path} required`);
    } else if (parsed.length == 2) {
      const last_component = parsed[parsed.length - 1];
      if (last_component === "audio" || last_component === "video") {
        throw new Error(`invalid path: ${path}: has single component local_path but the component seems to be media kind`);
      }
      return { cpath: path + "/", kind: undefined };
    } else {
      const last_component = parsed[parsed.length - 1];
      if (last_component.length > 0) {
        if (last_component !== "audio" && last_component !== "video") {
          return { cpath: path + "/", kind: undefined };
        }
        return {cpath: path, kind: last_component};
      }
      return { cpath: path, kind: undefined };
    }
  }

  async watchMedia(path: string, params: QRPCMediaReceiverParams): Promise<QRPCTrack[]> {
    const {onopen, onclose, onpause, onresume, initOptions} = params;
    if (!this.#handshaked()) {
      throw new Error("watchMedia can only be called after handshake");
    }
    const {cpath, kind} = this.#canonicalViewPath(path);
    const tracks = [];
    for (const k of (kind ? [kind] : ["video", "audio"])) {
      const path = kind ? cpath : QRPCTrack.path(cpath, k);
      const media = this.#addMedia(cpath, "recv", initOptions);
      let track = this.tracks[path];
      if (!track) {
        if (initOptions?.sync) { throw new Error(`no track for ${path} yet but sync option is set`); }
        track = new QRPCTrack(path, media, null, null, [], {
          onopen, onclose, onpause, onresume
        });
        // console.log("watchMedia: add track for", path);
        this.tracks[path] = track;
      }
      tracks.push(track);
    }
    // sdpGen/sentTracks/remoteOffer should be timing matched. otherwise other media API call
    // may change sdpGen/sentTracks which does not match with current values
    // same as sdpGen/sentTrack in openMedia, #handshake
    const sdpGen = this.#incSdpGen();
    const sentTracks = [...this.sentTracks];
    try {
      const remoteOffer = await this.syscall("consume", {path: cpath, initOptions});
      await this.#setRemoteOffer(remoteOffer, sdpGen, sentTracks);
    } catch (e) {
      console.log("watchMedia: remote negotiation failed", e);
      for (const t of tracks) {
        delete this.medias[t.media.path];
        delete this.tracks[t.path];
        await t.close(this.pc!);
      }
      throw e;
    }
    return tracks;
  }

  async pauseMedia(path: string): Promise<void> {
    const t = this.tracks[path];
    if (t) {
      await this.syscall("pause", { path });
      t.pause(QRPCTrack.PAUSE_REASON.local_op);
    } else {
      throw new Error("pauseMedia: no media for " + path);
    }
  }

  async resumeMedia(path: string): Promise<void> {
    const t = this.tracks[path];
    if (t) {
      await this.syscall("resume", { path });
      t.resume(QRPCTrack.PAUSE_REASON.local_op);
    } else {
      throw new Error("resumeMedia: no media for " + path);
    }
  }

  async updateMedia(path: string, options: {stream: MediaStream}): Promise<QRPCTrack[]> {
    const {stream} = options;
    const cpath = this.#canonicalOpenPath(path);
    const m = this.medias[cpath];
    if (!m) {
      throw new Error("updateMedia: no media for " + cpath);
    }
    
    const tracks: QRPCTrack[] = [];
    stream.getTracks().forEach(t => {
      const track_path = QRPCTrack.path(cpath, t.kind);
      const track = this.tracks[track_path];
      if (track) {
        track.update({stream, track: t});
        tracks.push(track);
      } else {
        console.log(`no track for ${path}`);
      }
    });
    
    return tracks;
  }

  async #closeTracks(paths: string[]): Promise<void> {
    const medias: {[path: string]: QRPCMedia} = {};
    
    for (const path of paths) {
      const t = this.tracks[path];
      if (t) {
        medias[t.media.path] = t.media;
        await t.close(this.pc!); // this removes the track from media.tracks
        delete this.tracks[path];
      }
      const index = this.sentTracks.indexOf(t);
      if (index >= 0) {
        this.sentTracks.splice(index, 1);
      }
    }
    // if media has no tracks, remove it too
    for (const k in medias) {
      const m = medias[k];
      if (Object.keys(m.tracks).length === 0) {
        console.log("close media", m.path);
        delete this.medias[m.path];
      }
    }
  }

  async closeMedia(path: string): Promise<void> {
    const remoteOffer = await this.syscall("close_media", { path });
    const sdpGen = this.#incSdpGen();
    const sentTracks = [...this.sentTracks];
    await this.#setRemoteOffer(remoteOffer, sdpGen, sentTracks);
  }

  openStream(path: string, params: QRPCStreamParams): RTCDataChannel {
    if (this.streams[path]) {
      return this.streams[path];
    }
    const s = this.pc!.createDataChannel(path, params as RTCDataChannelInit);
    this.#setupStream(s, params);
    return s;
  }

  closeStream(path: string): void {
    const s = this.streams[path];
    if (!s) {
      console.log(`No stream for path ${path}`);
      return;
    }
    s.close();
    delete this.streams[path];
  }

  watchStream(path: string, params: QRPCStreamParams): RTCDataChannel {
    return this.openStream(`$watch/${path}`, params);
  }

  async syscall(fn: string, args?: QRPCSyscallArgs): Promise<any> {
    return new Promise((resolve, reject) => {
      const msgid = this.#newMsgId();
      this.syscallStream!.send(JSON.stringify({fn, args, msgid}));
      this.rpcPromises[msgid] = { resolve, reject };
    });
  }

  #setupStream(s: RTCDataChannel, h: QRPCStreamParams): void {
    const path = s.label;
    const { onopen, onclose, onmessage, onerror } = h;
    s.onopen = (onopen && (async (event) => {
      if (h.publish) { await this.syscall("publish_stream",{path}); }
      const ctx = await promisify(onopen(s, event));
      if (ctx === false || ctx === null) {
        console.log(`close stream by application path=${path}`);
        this.closeStream(path);
        return;
      } else {
        (s as any).context = ctx;
      }
    })) || (async (event) => {
      if (h.publish) { await this.syscall("publish_stream",{path}); }
    });
    s.onclose = (onclose && ((event) => {
      onclose(s, event);
      delete this.streams[path];
    })) || ((event) => {
      delete this.streams[path];
    });
    s.onerror = (onerror && ((event) => {
      onerror(s, event);
      delete this.streams[path];
    })) || ((event) => {
      delete this.streams[path];
    });
    s.onmessage = (event) => onmessage(s, event);
    this.streams[path] = s;
  }
}
