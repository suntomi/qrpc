class QRPCTrack {
  static DAFAULT_TRACK_RECONNECTION_WAIT_MS = 5000;
  static PAUSE_REASON = {
    remote_close: "remote_close",
    local_op: "local_op",
    remote_op: "remote_op",
  };
  static path(canonical_path, kind) { return canonical_path + kind; }
  constructor(path, media, stream, track, encodings, {onopen, onclose, onupdate, onpause, onresume}) {
    this.path = path;
    this.media = media;
    this.media.addTrack(this);
    this.stream = stream;
    this.encodings = encodings;
    this.onopen = onopen;
    this.onclose = onclose;
    this.onpause = onpause || ((reason) => {});
    this.onresume = onresume || (() => {});
    this.onupdate = onupdate || (() => {});
    this.track = track
    this.opened = false;
    this.pausedReasons = [];
  }
  get id() { return this.track.id; }
  get kind() { 
    const parsed = this.path.split("/");
    return parsed[parsed.length - 1];
  }
  get raw() { return this.track; }
  get cname() {
    const parsed = this.path.split("/");
    if (!this.isReceiver) { throw new Error("cname is only for receiver track"); }
    return parsed[0];
  }
  get directory() { return this.path.split("/").slice(0, -1).join("/"); }
  get mid() { return this.transceiver.mid; }
  get active() { return this.track != null; }
  get paused() { return this.pausedReasons.length > 0; }
  get direction() { return this.media.direction; }
  get isReceiver() { return this.media.isReceiver; }
  pausedBy(reason) {
    return this.pausedReasons.indexOf(reason) >= 0;
  }
  pause(reason, noCallback) {
    const i = this.pausedReasons.indexOf(reason);
    if (i < 0) {
      this.pausedReasons.push(reason);
    }
    return !noCallback && this.onpause && this.onpause(this, reason);
  }
  resume(reason, noCallback) {
    const i = this.pausedReasons.indexOf(reason);
    if (i >= 0) {
      this.pausedReasons.splice(i, 1);
      !noCallback && this.onresume && this.onresume(this, reason);
    }
  }
  update({stream, track}) {
    if (!this.transceiver) {
      throw new Error("track is not started");
    }
    this.stream = stream;
    this.transceiver.sender.replaceTrack(track);
    this.track?.stop();
    this.track = track;
    this.onupdate(this);
  }
  async open(pc, midMediaPathMap) {
    if (this.direction !== "send" || this.track == null) {
      throw new Error("open is only needed for send tracks");
    }
    if (midMediaPathMap) { // want to put tracks to actual peer connection (not for generating localOffer for producing)
      let transceiver; // find transceiver for this track by comparing logical path of the track and path decided by server mid
      for (const t of pc.getTransceivers()) {
        if (t.sender == null) {
          console.log("ignore receiver transceiver", t.sender.track);
          continue;
        }
        // in here, pc.setRemoteDescription is already called, so mid is decided by server remote offer
        // also midMediaPathMap is updated by syscall ("produce") or whip API call (in QRPClient.#handshake)
        const path = midMediaPathMap[t.mid];
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
        await transceiver.sender.setParameters(Object.assign(params,{encodings:this.encodings}));
      }
      await transceiver.sender.replaceTrack(this.track);
      this.transceiver = transceiver;
    } else {
      if (this.track.kind === "video") {
        this.transceiver = pc.addTransceiver(
          this.track,
          {direction: 'sendonly', sendEncodings: this.encodings, streams: [this.stream]}
        );
      } else if (this.track.kind === "audio") {
        this.transceiver = pc.addTransceiver(
          this.track,
          {direction: 'sendonly', streams: [this.stream]}
        );
      } else {
        throw new Error(`invalid track kind ${this.track.kind}`);
      }
    }
  }
  async close(pc, force) {
    if (this.track) {
      this.onclose(this);
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
class QRPCMedia {
  constructor(path, direction, options) {
    this.path = path;
    this.direction = direction;
    this.options = options;
    this.tracks = {};
    this.nextReconnect = 0;
    this.lastPing = null;
    this.reconnectInvtervalMS = null;
    this.opened = true;
    this.keepAlive();
  }
  get isReceiver() { return this.direction === "recv"; }
  keepAlive() {
    this.lastPing = (new Date()).getTime();
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
      let reconnectionWaitMS = this.tracks[k].pause(reason);
      if (reason === QRPCTrack.PAUSE_REASON.remote_close) {
        if (!reconnectionWaitMS && reconnectionWaitMS !== false && reconnectionWaitMS !== null) {
          reconnectionWaitMS = QRPCTrack.DAFAULT_TRACK_RECONNECTION_WAIT_MS;
        } else if (typeof reconnectionWaitMS !== "number") {
          reconnectionWaitMS = null;
        }
        if (reconnectionWaitMS) {
          ret.push(reconnectionWaitMS);
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
    this.reconnectInvtervalMS = backoffMS
    this.nextReconnect = (new Date()).getTime() + this.reconnectInvtervalMS;
    this.reconnect(c);
  }
  stopReconnect() {
    this.reconnectInvtervalMS = null;
    this.nextReconnect = null;
  }
  async reconnect(c) {
    try {
      console.log(`try reconnect for ${this.path}`);
      await c.watchMedia(this.path, {
        options: Object.assign(this.options, { sync: true })
      }, true);
    } catch (e) {
      console.log(`reconnect failed for ${this.path}: ${e.message}`);
    }
  }
}
class QRPClient {
  static SYSCALL_STREAM = "$syscall";
  static DEFAULT_SCALABILITY_MODE = "L1T3";
  static NO_INPUT_THRESHOLD = 3;
  static VERBOSE_SYSCALL = ["ping", "ping_ack"];
  static MAX_MSGID = Number.MAX_SAFE_INTEGER;
  constructor(url, cname) {
    this.url = url;
    this.cert = null;
    this.cname = cname || this.#genCN();
    console.log("QRPClient", this.cname, url);
    this.reconnect = 0;
    this.#clear();
  }
  async #syscallMessageHandler(s, event) {
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
          if (t) {
            t.media.keepAlive();
          } else {
            console.log(`no such track for ping: ${data.args.path}`);
          }
        }
      } else {
        throw new Error("unknown syscall: " + JSON.stringify(data));
      }
    } else {
      const promise = this.#fetchPromise(data.msgid);
      if (!promise) {
        console.log(`promises for msgid:${data.msgid} does not exist`);
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
  }
  #genCN() {
    const bytes = new Uint8Array(8);
    crypto.getRandomValues(bytes);
    return btoa(String.fromCharCode(...bytes))
      // make result url safe
      .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  }
  #clear() {
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
  initIce() {
    //Ice properties
    this.iceUsername = null;
    this.icePassword = null;
    //Pending candidadtes
    this.candidates = [];
    this.endOfcandidates = false;
  }
  #newMsgId() {
    const msgid = this.msgidSeed++;
    if (this.msgidSeed > QRPClient.MAX_MSGID) {
      this.msgidSeed = 1;
    }
    return msgid;
  }
  #fetchPromise(msgid) {
    const promise = this.rpcPromises[msgid];
    delete this.rpcPromises[msgid];
    return promise;
  }
  #handshaked() {
    return this.syscall_ready;
  }
  #incSdpGen() {
    this.sdpGen++;
    console.log(`current sdpGen:${this.sdpGen}`, this.#handshaked());
    return this.sdpGen;
  }
  get connected() {
    return this.pc?.connectionState === "connected";
  }
  async connect() {
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
    const waiter = new Promise((resolve) => {
      this.#setupCallbacks(pc, resolve);
    });
    await this.#handshake(waiter);
  }
  async #createPeerConnection() {
    if (!this.cert) {
      this.cert = await RTCPeerConnection.generateCertificate({
        name: 'ECDSA',
        namedCurve: 'P-256'
      });
    }
    // always uses same cert for peer connection
    return new RTCPeerConnection({
      iceServers: [],
      certificates: [this.cert]
    });
  }
  #setupCallbacks(pc, resolve) {
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
        const r = t.onopen(t);
        if (r === false || r === null) {
          console.log(`close media by application ${path}`);
          this.closeMedia(path);
          return;
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
          resolve();
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
        if (event.candidate.sdpMLineIndex>0) {
          return;
        }
        this.candidates.push(event.candidate);                                         
      } else {
        // No more candidates
        this.endOfcandidates = true;
      }
    }
    this.timer = setInterval(() => {
      const now = (new Date()).getTime();
      this.#checkMedias(now);
    }, 1000); // 1秒ごとにチェック
  }
  parseLocalOffer(localOffer) {
    const result = {}
    let currentMid, currentSsrcs;
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
        console.log("ssrc/attrName", ssrc, attrName);
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
  #checkMedias(now) {
    let mediaOpened = false;
    for (const k in this.medias) {
      const m = this.medias[k];
      if (m.isReceiver) {
        // console.log("check track", t.path, now - t.lastPing);
        if (now - m.lastPing > (QRPClient.NO_INPUT_THRESHOLD * 1000)) {
          if (m.opened) {
            m.opened = false;
            const reconnectionWaitMS = m.pause(QRPCTrack.PAUSE_REASON.remote_close);
            console.log(`no ping for ${m.path} for ${QRPClient.NO_INPUT_THRESHOLD * 1000} ms`, reconnectionWaitMS);
            if (reconnectionWaitMS) {
              m.startReconnect(this, reconnectionWaitMS);
              console.log(`track ${m.path} will try reconnect every ${reconnectionWaitMS} ms`);
            }
          } else if (m.reconnectInvtervalMS && now > m.nextReconnect) {
            m.reconnect(this);
            m.nextReconnect = now + m.reconnectInvtervalMS;
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
  async #createOffer(tracks) {
    const midPathMap = {};
    // create dummy peer connection to generate sdp
    const pc = await this.#createPeerConnection();
    // emurate creating stream to generate correct sdp
    pc.createDataChannel("dummy");
    for (const t of tracks) {
      await t.open(pc);
    }
    const localOffer = await pc.createOffer();
    await pc.setLocalDescription(localOffer);
    const midSsrcMap = this.parseLocalOffer(localOffer.sdp);
    // console.log("midSsrcMap", midSsrcMap);
    for (const t of tracks) {
      // now, mid is decided by calling pc.setLocalDescription. 
      // mid (in server-responded remote offer) probably changes 
      // after it processes on server side, but because of client mid actually decided
      // by server remote offer, changes causes no problem.
      midPathMap[t.mid] = t.path;
      t.ssrc = midSsrcMap[t.mid] || undefined;
      // console.log(`${t.path},mid=${t.mid} ssrc = ${t.ssrc}`);
    }
    pc.close();
    return {localOffer, midPathMap};
  }
  #fixupLocalAnswer(localAnswer, midSsrcMap) {
    // console.log("#fixupLocalAnswer", midSsrcMap);
    const chunks = [];
    let chunk = [];
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
  async #setRemoteOffer(remoteOffer, sdpGen, sentTracks) {
    console.log("remote offer sdp", remoteOffer, sdpGen);
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
    const midSsrcMap = {};
    for (const t of sentTracks) {
      await t.open(this.pc, this.midMediaPathMap);
      if (t.ssrc) {
        midSsrcMap[t.mid] = t.ssrc;
      }
    }
    const answer = await this.pc.createAnswer();
    // answer.sdp = this.#fixupLocalAnswer(answer.sdp, midSsrcMap);
    // console.log("local answer sdp", answer.sdp);
    if (this.#handshaked()) {
      const newMidSsrcMap = this.parseLocalOffer(answer.sdp);
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
        t.ssrc = newMidSsrcMap[t.mid] || undefined;
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
    this.iceUsername = localOffer.sdp.match(/a=ice-ufrag:(.*)[\r\n]+/)[1];
    this.icePassword = localOffer.sdp.match(/a=ice-pwd:(.*)[\r\n]+/)[1];

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
      console.log("error in handling whip response:" + e.message + "|" + (text || "no response"));
      throw e;
    }
  }
  async close() {
    await this.#close(true);
  }
  async #close(fromLocal) {
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
  #canonicalOpenPath(path) {
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
  async openMedia(path, {stream, encodings, options, onopen, onclose, onupdate, onpause, onresume}) {
    const cpath = this.#canonicalOpenPath(path);
    console.log("openMedia", cpath, stream, encodings);
    if (!encodings) { throw new Error("encodings is mandatory"); }
    if (encodings.length > 3) { throw new Error("encodings more than 3 may not be treated correctly"); }
    const ridScalabilityModeMap = {};
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
    encodings.sort((a, b) => a.maxBitrate - b.maxBitrate);
    const tracks = [];
    stream.getTracks().forEach(t => {
      const path = QRPCTrack.path(cpath, t.kind);
      const media = this.#addMedia(cpath, "send", options);
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
          sdp: localOffer.sdp, options, midPathMap, rtp: {ridScalabilityModeMap}
        });
        await this.#setRemoteOffer(remoteOffer, sdpGen, sentTracks);
      } catch (e) {
        console.log("openMedia: remote negotiation failed", e.message);
        for (const t of tracks) {
          delete this.medias[t.media.path];
          delete this.tracks[t.path];
          await t.close(this.pc);
        }
        throw e;
      }
    }
    return tracks;
  }
  #addMedia(path, direction, options) {
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
      media = new QRPCMedia(path, direction, options);
      this.medias[path] = media;
    }
    return media;
  }
  // media_path is one of the following:
  // 1. ${cname}/${local_path}/ => all media kind under local_path consumed
  // 2. ${cname}/${local_path}/${media_kind} => only media_kind under local_path consumed
  // for 1. last / is mandatory to indicate that it is a directory.
  // but / can be omitted if last component of local_path does not ssem to be a media kind (not audio/video)
  #canonicalViewPath(path) {
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
  async watchMedia(path, {onopen, onclose, onpause, onresume, options}) {
    if (!this.#handshaked()) {
      throw new Error("watchMedia can only be called after handshake");
    }
    const {cpath, kind} = this.#canonicalViewPath(path);
    const tracks = [];
    for (const k of (kind ? [kind] : ["video", "audio"])) {
      const path = kind ? cpath : QRPCTrack.path(cpath, k);
      const media = this.#addMedia(cpath, "recv", options);
      let track = this.tracks[path];
      if (!track) {
        if (options?.sync) { throw new Error(`no track for ${path} yet but sync option is set`); }
        track = new QRPCTrack(path, media, null, null, undefined, {
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
      const remoteOffer = await this.syscall("consume", {path: cpath, options});
      await this.#setRemoteOffer(remoteOffer, sdpGen, sentTracks);
    } catch (e) {
      console.log("watchMedia: remote negotiation failed", e.message);
      for (const t of tracks) {
        delete this.medias[t.media.path];
        delete this.tracks[t.path];
        await t.close(this.pc);
      }
      throw e;
    }
    return tracks;
  }
  async pauseMedia(path) {
    const t = this.tracks[path];
    if (t) {
      await this.syscall("pause", { path });
      t.pause(QRPCTrack.PAUSE_REASON.local_op);
    } else {
      throw new Error("pauseMedia: no media for " + path);
    }
  }
  async resumeMedia(path) {
    const t = this.tracks[path];
    if (t) {
      await this.syscall("resume", { path });
      t.resume(QRPCTrack.PAUSE_REASON.local_op);
    } else {
      throw new Error("resumeMedia: no media for " + path);
    }
  }
  async updateMedia(path, {stream}) {
    const cpath = this.#canonicalOpenPath(path);
    const m = this.medias[cpath];
    if (!m) {
      throw new Error(`no media for ${cpath}`);
    }
    const tracks = [];
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
  async #closeTracks(paths) {
    const medias = [];
    for (const path of paths) {
      const t = this.tracks[path];
      if (t) {
        medias[t.media.path] = t.media;
        await t.close(this.pc); // this removes the track from media.tracks
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
  async closeMedia(path) {
    const remoteOffer = await this.syscall("close_media", { path });
    const sdpGen = this.#incSdpGen();
    const sentTracks = [...this.sentTracks];
    await this.#setRemoteOffer(remoteOffer, sdpGen, sentTracks);
  }
  // options combines createDataChannel's option and RTCDataChannel event handler (onopen, onclose, onmessage)
  openStream(path, options) {
    if (this.streams[path]) {
      return this.streams[path];
    }
    const s = this.pc.createDataChannel(path, options);
    this.#setupStream(s, options);
    return s;
  }
  closeStream(path) {
    const s = this.streams[path];
    if (!s) {
      console.log(`No stream for path ${path}`);
      return;
    }
    s.close();
    delete this.streams[path];
  }
  watchStream(path, options) {
    return this.openStream(`$watch/${path}`, options);
  }
  async syscall(fn, args) {
    return new Promise((resolve, reject) => {
      const msgid = this.#newMsgId();
      this.syscallStream.send(JSON.stringify({fn, args, msgid}));
      this.rpcPromises[msgid] = { resolve, reject };
    });
  }
  #setupStream(s, h) {
    const path = s.label;
    s.onopen = (h.onopen && (async (event) => {
      if (h.publish) { await this.syscall("publish_stream",{path}); }
      const ctx = h.onopen(s, event);
      if (ctx === false || ctx === null) {
        console.log(`close stream by application path=${path}`);
        this.closeStream(path);
        return;
      } else {
        s.context = ctx;
      }
    })) || (async (event) => {
      if (h.publish) { await this.syscall("publish_stream",{path}); }
    });
    s.onclose = (h.onclose && ((event) => {
      h.onclose(s, event);
      delete this.streams[path];
    })) || ((event) => {
      delete this.streams[path];
    });
    s.onerror = (h.onerror && ((event) => {
      h.onerror(s, event);
      delete this.streams[path];
    })) || ((event) => {
      delete this.streams[path];
    });
    s.onmessage = (event) => h.onmessage(s, event);
    this.streams[path] = s;
  }
};