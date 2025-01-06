class QRPCTrack {
  static path(parent_path, kind) { return parent_path + "/" + kind; }
  constructor(path, stream, track, encodings, {onopen, onclose, onpause, onresume}) {
    this.path = path;
    this.stream = stream;
    this.encodings = encodings;
    this.onopen = onopen;
    this.onclose = onclose;
    this.onpause = onpause || (() => {});
    this.onresume = onresume || (() => {});
    this.track = track
    this.direction = track ? "send" : "recv";
    this.opened = false;
    this.paused = false;
  }
  get id() { return this.track.id; }
  get kind() { return this.track.kind; }
  get raw() { return this.track; }
  get directory() { return this.path.split("/").slice(0, -1).join("/"); }
  get mid() { return this.transceiver.mid; }
  get active() { return this.track != null; }
  async open(pc, midMediaPathMap) {
    if (this.direction !== "send" || this.track == null) {
      throw new Error("open is only needed for send tracks");
    }
    if (midMediaPathMap) { // want to put tracks to actual peer connection (not for generating localOffer for producing)
      let transceiver;
      for (const t of pc.getTransceivers()) {
        if (t.sender == null) {
          console.log("ignore receiver transceiver", t.sender.track);
          continue;
        }
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
  close() {
    if (this.track) {
      this.onclose(this);
      this.track.stop();
      this.track = null;
      this.transceiver = null;
      this.stream = null;
      this.opened = false;
    }
  }
}
class QRPClient {
  static SYSCALL_STREAM = "$syscall";
  static DEFAULT_SCALABILITY_MODE = "L1T3";
  static NO_INPUT_THRESHOLD = 1;
  static MAX_MSGID = Number.MAX_SAFE_INTEGER;
  constructor(url, cname) {
    this.url = url;
    this.cert = null;
    const bytes = new Uint8Array(8);
    this.cname = cname || this.#genCN();
    console.log("QRPClient", this.cname, url);
    this.reconnect = 0;
    this.#clear();
  }
  async #syscallMessageHandler(s, event) {
    const data = JSON.parse(event.data);
    if (!data.msgid) {
      if (data.fn === "close") {
        console.log("shutdown by server");
        this.#close();
      } else if (data.fn === "close_track") {
        for (const path of data.args.paths) {
          console.log("close track", path);
          this.closeMedia(path);
        }
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
        if (!data.args.ssrc_label_map) {
          promise.reject(new Error(`invalid response: no ssrc_label_map: ${JSON.stringify(data.args)}`));
          return;
        }
        for (const pair of data.args.ssrc_label_map || []) {
          this.ssrcLabelMap[pair[0]] = pair[1];
        }
        Object.assign(this.midMediaPathMap, data.args.mid_media_path_map || {});
        console.log("midMediaPathMap => ", this.midMediaPathMap);
        console.log("ssrcLabelMap => ", this.ssrcLabelMap);
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
        promise.resolve(data.args.sdp);
      } else if (
        data.fn == "resume_ack" || data.fn == "pause_ack" || data.fn == "close_ack"
      ) {
        promise.resolve();
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
    this.tracks = {};
    this.sentTracks = [];
    this.trackIdLabelMap = {};
    this.ridLabelMap = {};
    this.midMediaPathMap = {};
    this.ssrcLabelMap = {};
    this.ridScalabilityModeMap = {};
    this.rpcPromises = {};
    this.sdpGen = -1;
    this.msgidSeed = 1;
    this.id = null;
    this.syscallStream = null;
    this.timer = null;
    this.recvStats = {};
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
    return this.sdpGen >= 0;
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
    this.#setupCallbacks(pc);
    await this.#handshake();
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
  #setupCallbacks(pc) {
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
      // TODO: unify this step by using event.transceiver.mid and this.midMediaPathMap
      let path = undefined;
      if (event.transceiver) {
        if (event.transceiver.mid === "probator") { console.log("ignore probator"); return; }
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
      if (!t.active) {
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
    pc.onicecandidate = (event)=>{
      if (event.candidate) 
      {
        console.log("Ice candidate", event.candidate);
        //Ignore candidates not from the first m line
        if (event.candidate.sdpMLineIndex>0)
          //Skip
          return;
        //Store candidate
        this.candidates.push(event.candidate);                                         
      } else {
        console.log("no more Ice candidate", event.candidate);
        //No more candidates
        this.endOfcandidates = true;
      }
    }
    this.timer = setInterval(async () => {
      await this.#checkTrackInput();
    }, 1000); // 1秒ごとにチェック
  }
  parseLocalOffer(localOffer) {
    const result = {}
    let currentMid, currentSsrc;
    const lines = localOffer.split(/\r?\n/);
    lines.push("m=dummy"); // ensure last section processed
    for (const l of lines) {
      if (l.startsWith("m=")) {
        currentMid = undefined;
        currentSsrc = undefined;
      } else if (l.startsWith("a=mid:")) {
        if (!currentMid) {
          // a=mid:$mid_value
          currentMid = l.slice(6).trim();
          if (currentSsrc) {
            result[currentMid] = currentSsrc;
          }
        } else {
          throw new Error(`invalid find a=mid line twice before reset by m= line ${currentMid},${l.slice(6)}`);
        }
      } else if (l.startsWith("a=ssrc:")) {
        currentSsrc = l.slice(7).split(/\s/,1)[0].trim();
        if (currentMid) {
          result[currentMid] = currentSsrc;
        }
      }
    }
    return result;
  }
  async #checkTrackInput() {
    const recvStats = this.recvStats;
    const stats = await this.pc.getStats();
    for (const report of stats.values()) {
      if (report.type !== 'inbound-rtp') {
        continue;
      }
      const path = report.trackIdentifier;
      const track = this.tracks[path];
      if (!track || track.direction !== "recv") {
        continue;
      }
      if (!recvStats[path]) {
        recvStats[path] = { packetsReceived: 0, noInput: 0 };
      }
      const packetsReceived = report.packetsReceived;
      const lastPacketsReceived = recvStats[path].packetsReceived;
      const packetsPerSecond = packetsReceived - lastPacketsReceived;
      if (packetsPerSecond <= 0) {
        recvStats[path].noInput++;
        if (recvStats[path].noInput > QRPClient.NO_INPUT_THRESHOLD) {
          if (!track.paused) {
            console.log(`no input for ${path} for ${QRPClient.NO_INPUT_THRESHOLD} seconds`);
            track.paused = true;
            track.onpause(track);
          }
        }
      } else {
        recvStats[path].noInput = 0;
        if (track.paused) {
          console.log(`input again for ${path}`);
          track.paused = false;
          track.onresume(track);
        }
      }
      recvStats[path].packetsReceived = packetsReceived;
      // console.log(`in packets received per second: ${id} = ${packetsPerSecond} (${recvStats[path].noInput})`);
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
    console.log("midSsrcMap", midSsrcMap);
    for (const t of tracks) {
      // now, mid is decided. mid (in server remote offer) probably changes 
      // after it processes on server side, but because of client mid also decided
      // by server remote offer, changes causes no problem.
      midPathMap[t.mid] = t.path;
      t.ssrc = midSsrcMap[t.mid] || undefined;
      console.log(`${t.path},mid=${t.mid} ssrc = ${t.ssrc}`);
    }
    pc.close();
    return {localOffer, midPathMap};
  }
  modifyLocalAnswer(localAnswer, midSsrcMap) {
    console.log("modifyLocalAnswer", midSsrcMap);
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
  async #setRemoteOffer(remoteOffer) {
    console.log("remote offer sdp", remoteOffer);
    //set remote description
    await this.pc.setRemoteDescription({type:"offer",sdp:remoteOffer});
    //set tracks to actual peer connection
    const midSsrcMap = {};
    for (const t of this.sentTracks) {
      await t.open(this.pc, this.midMediaPathMap);
      if (t.ssrc) {
        midSsrcMap[t.mid] = t.ssrc;
      }
    }
    //Create the answer
    const answer = await this.pc.createAnswer();
    answer.sdp = this.modifyLocalAnswer(answer.sdp, midSsrcMap);
    console.log("local answer sdp", answer.sdp);
    await this.pc.setLocalDescription(answer);
  }
  async #capability() {
    const pc = new RTCPeerConnection();
    pc.addTransceiver("audio");
    pc.addTransceiver("video");
    const offer = await pc.createOffer({offerToReceiveAudio: true, offerToReceiveVideo: true});
    console.log("capability sdp", offer.sdp);
    return offer.sdp;
  }
  async #handshake() {
    if (this.#handshaked()) {
      throw new Error("handshake only called once in session");
    }
    // generate syscall stream (it also ensures that SDP for data channel is generated)
    this.syscallStream = this.openStream(QRPClient.SYSCALL_STREAM, {
      onmessage: this.#syscallMessageHandler.bind(this)
    });
    if (this.onopen) {
      this.context = await this.onopen();
    }
    // Create new SDP offer without initializing actual peer connection
    const {localOffer, midPathMap: localMidLabelMap} = await this.#createOffer(this.sentTracks);
    console.log("local offer sdp", localOffer.sdp, localMidLabelMap);
    // const oldSdp = this.pc.localDescription;
    // // (re)Set local description
    // await this.pc.setLocalDescription(offer);

    //store local ice ufrag/pwd
    this.iceUsername = localOffer.sdp.match(/a=ice-ufrag:(.*)[\r\n]+/)[1];
    this.icePassword = localOffer.sdp.match(/a=ice-pwd:(.*)[\r\n]+/)[1];

    //Do the post request to the WHIP endpoint with the SDP offer
    const fetched = await fetch(this.url, {
      method: "POST",
      body: JSON.stringify({
        sdp:localOffer.sdp,
        cname:this.cname,
        rtp:this.#rtpPayload(),
        midPathMap: localMidLabelMap,
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
      this.sdpGen++;

      this.id = remoteOffer.match(/a=ice-ufrag:(.*)[\r\n]+/)[1];
      console.log("id", this.id);

      await this.#setRemoteOffer(remoteOffer);
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
      // send close message to server
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
      this.tracks[k].close();
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
  #rtpPayload() {
    if (
      Object.keys(this.ridLabelMap).length !== 0 ||
      Object.keys(this.ridScalabilityModeMap).length !== 0 ||
      Object.keys(this.trackIdLabelMap).length !== 0
    ) {
      return {
        ridLabelMap:this.ridLabelMap,
        ridScalabilityModeMap:this.ridScalabilityModeMap,
        trackIdLabelMap:this.trackIdLabelMap,
      };
    }
    return undefined;
  }
  #canonicalOpenPath(path) {
    const parsed = path.split('/');
    if (parsed.length == 1) {
      return path;
    } else {
      if (parsed[parsed.length - 1].length > 0) {
        throw new Error(`invalid path: ${path}: should be ended with /`);
      }
      return path.slice(0, -1);
    }
  }
  async openMedia(path, {stream, encodings, options, onopen, onclose, onpause, onresume}) {
    const cpath = this.#canonicalOpenPath(path);
    console.log("openMedia", cpath, stream, encodings);
    if (!encodings) {
      throw new Error("encoding is mandatory");
    }
    encodings.forEach(e => {
      if (!e.rid) { throw new Error("for each encodings, rid is mandatory"); }
      if (!e.maxBitrate) { throw new Error("for each encodings, maxBitrate is mandatory"); }
      e.scalabilityMode = e.scalabilityMode || QRPClient.DEFAULT_SCALABILITY_MODE;
      this.ridLabelMap[e.rid] = cpath;
      this.ridScalabilityModeMap[e.rid] = e.scalabilityMode;
    });
    // sort by maxBitrate asc, because server regards earlier encoding as lower quality,
    // regardless its actual bitrate
    encodings.sort((a, b) => a.maxBitrate - b.maxBitrate);
    const tracks = [];
    stream.getTracks().forEach(t => {
      const path = QRPCTrack.path(cpath, t.kind);
      this.trackIdLabelMap[t.id] = path;
      const track = new QRPCTrack(path, stream, t, encodings, {onopen, onclose, onpause, onresume});
      console.log("openMedia: add track for", track.path);
      this.tracks[track.path] = track;
      this.sentTracks.push(track);
      tracks.push(track);
    });
    if (this.#handshaked()) {
      // already handshaked, so renegotiate for newly produced tracks.
      const {localOffer, midPathMap} = await this.#createOffer(tracks);
      console.log("openMedia: local offer", localOffer.sdp, midPathMap);
      const remoteOffer = await this.syscall("produce", { 
        sdp: localOffer.sdp, options, midPathMap
      });
      await this.#setRemoteOffer(remoteOffer);
    }
    return tracks;
  }
  #canonicalViewPath(path) {
    const parsed = path.split('/');
    if (parsed.length < 2) {
      throw new Error(`invalid path: ${path}: at least \${cname}/\${single_component_local_path} required`);
    } else if (parsed.length == 2) {
      return { cpath: path, kind: undefined };
    } else {
      const last_component = parsed[parsed.length - 1];
      if (last_component.length > 0) {
        if (last_component !== "audio" && last_component !== "video") {
          throw new Error(`invalid path: ${path}: should be ended with / or /audio or /video`);
        }
        return {cpath: parsed.slice(0, -1).join("/"), kind: last_component};
      }
      return { cpath: path.slice(0, -1), kind: undefined };
    }
  }
  async viewMedia(path, {onopen, onclose, onpause, onresume, audio, video}) {
    const {cpath, kind } = this.#canonicalViewPath(path);
    const remoteOffer = await this.syscall("consume", { 
      path: cpath, options: (audio || video) ? { audio, video } : undefined
    });
    const tracks = [];
    for (const k of (kind ? [kind] : ["video", "audio"])) {
      const path = QRPCTrack.path(cpath, k);
      const track = new QRPCTrack(path, null, null, undefined, {onopen, onclose, onpause, onresume});
      console.log("viewMedia: add track for", path);
      this.tracks[path] = track;
      tracks.push(track);
    }
    await this.#setRemoteOffer(remoteOffer);
    return tracks;
  }
  async pauseMedia(path) {
    console.log("pause", path);
    const t = this.tracks[path];
    if (t) {
      await this.syscall("pause", { path });
      t.onpause && t.onpause(t);
    } else {
      throw new Error("pauseMedia: no media for " + path);
    }
  }
  async resumeMedia(path) {
    console.log("resume", path);
    const t = this.tracks[path];
    if (t) {
      await this.syscall("resume", { path });
      t.onresume && t.onresume(t);
    } else {
      throw new Error("resumeMedia: no media for " + path);
    }
  }
  closeMedia(path) {
    const t = this.tracks[path];
    if (t) {
      t.close();
      delete this.tracks[path];
    }
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
  async syscall(fn, args) {
    return new Promise((resolve, reject) => {
      const msgid = this.#newMsgId();
      this.syscallStream.send(JSON.stringify({fn, args, msgid}));
      this.rpcPromises[msgid] = { resolve, reject };
    });
  }
  #setupStream(s, h) {
    const path = s.label;
    s.onopen = (h.onopen && ((event) => {
      const ctx = h.onopen(s, event);
      if (ctx === false || ctx === null) {
        console.log(`close stream by application path=${path}`);
        this.closeStream(path);
        return;
      } else {
        s.context = ctx;
      }
    })) || ((event) => {});
    s.onclose = (h.onclose && ((event) => {
      h.onclose(s, event);
    })) || ((event) => {});
    s.onmessage = (event) => h.onmessage(s, event);
    this.streams[path] = s;
  }
};