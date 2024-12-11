class QRPCTrack {
  static key(label, kind) { return label + "/" + kind; }
  constructor(label, stream, track, encodings, onopen, onclose) {
    this.label = label;
    this.stream = stream;
    this.encodings = encodings;
    this.onopen = onopen;
    this.onclose = onclose;
    this.track = track
  }
  get id() { return this.track.id; }
  get key() { return QRPCTrack.key(this.label, this.track.kind); }
  get kind() { return this.track.kind; }
  get raw() { return this.track; }
  get mid() { return this.transceiver.mid; }
  get active() { return this.track != null; }
  open(pc) {
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
  close() {
    if (this.track) {
      this.onclose(this);
      this.track.stop();
      this.track = null;
    }
  }
}
class QRPClient {
  static SYSCALL_STREAM = "$syscall";
  static DEFAULT_SCALABILITY_MODE = "L1T3";
  static MAX_MSGID = Number.MAX_SAFE_INTEGER;
  constructor(url, cname) {
    this.url = url;
    const bytes = new Uint8Array(8);
    this.cname = cname || this.#genCN();
    console.log("QRPClient", this.cname, url);
    this.reconnect = 0;
    this.#clear();
  }
  async #syscallMessageHandler(s, event) {
    const data = JSON.parse(event.data);
    if (data.fn === "close") {
      console.log("shutdown by server");
      this.close();
    } else if (data.fn === "nego") {
      // our library basically exchange media stream via QRPC server
      // at least, we will have to implement WHIP endpoint of QRPC server for first exchanging SDP between peers.
      this.ridLabelMap = data.args.ridLabelMap;
      console.log("should not receive nego from server or other peer");
      await this.pc.setRemoteDescription({type:"offer",sdp:data.args.sdp});
      const answer = await this.pc.createAnswer();
      await this.pc.setLocalDescription(answer);
      this.syscall("nego_ack",{sdp:answer.sdp,gen:this.sdpGen,msgid:data.args.msgid});
    } else if (data.fn === "nego_ack") {
      const promise = this.#fetchPromise(data.args.msgid);
      if (!promise) {
        console.log(`promises for gen:${data.args.gen} does not exist`);
        return;
      }
      if (this.sdpGen > data.args.gen) {
        promise.reject(new Error(`old sdp anwser for gen:${data.args.gen} ignored`));
        return;
      } else if (this.sdpGen < data.args.gen) {
        promise.reject(new Error(`future sdp anwser for gen:${data.args.gen} ignored`));
        return;
      }
      if (data.args.error) {
        promise.reject(new Error(`invalid sdp ${offer.sdp}: ${data.args.error}`));
        return;
      } else {
        promise.resolve(data.args.sdp);
      }
    } else if (data.fn == "consume") {
      throw new Error("does not suported");
    } else if (data.fn == "consume_ack") {
      const promise = this.#fetchPromise(data.args.msgid);
      if (!promise) {
        console.log(`promises for gen:${data.args.gen} does not exist`);
        return;
      }
      if (data.args.error) {
        promise.reject(new Error(data.args.error));
        return;
      } else {
        if (!data.args.sdp) {
          promise.reject(new Error(`invalid response: no sdp: ${JSON.stringify(data.args)}`));
          return;
        }
        if (!data.args.ssrc_label_map) {
          promise.reject(new Error(`invalid response: no ssrc_label_map: ${JSON.stringify(data.args)}`));
          return;
        }
        for (const pair of data.args.ssrc_label_map) {
          this.ssrcLabelMap[pair[0]] = pair[1];
        }
        console.log("ssrc_label_map => ", this.ssrcLabelMap);
        promise.resolve(data.args.sdp);
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
    this.trackIdLabelMap = {};
    this.ridLabelMap = {};
    this.midLabelMap = {};
    this.ssrcLabelMap = {};
    this.ridScalabilityModeMap = {};
    this.rpcPromises = {};
    this.sdpGen = -1;
    this.msgidSeed = 1;
    this.id = null;
    this.mediaHandshaked = false;
    this.syscallStream = null;
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
    // create dummy peer connection to generate sdp
    return new RTCPeerConnection({
      iceServers: [],
      certificates: [this.cert]
    });
  }
  #setupCallbacks(pc) {
    // Listen for data channels
    pc.ondatachannel = (event) => {            
      const s = event.channel;
      console.log(`accept stream ${s.label}`);
      if (!this.onstream) {
        s.close();
        throw new Error("QRPClient.onstream is mandatory");
      }
      const h = this.onstream(s);
      if (!h) {
        console.log(`No stream callbacks for label [${s.label}]`);
        s.close();
        return;
      }
      this.#setupStream(s, h);
    };

    // Listen addition of media tracks
    pc.ontrack = async (event) => {
      console.log("ontrack", event);
      const track = event.track;
      const tid = track.id;
      const receiver = event.receiver;
      // RTCRtpReceiverの統計情報を取得
      const stats = await receiver.getStats();
      let label = undefined;
      for (const report of stats.values()) {
        console.log("report", report);
        if (report.type === 'inbound-rtp') {
          console.log(`track id: ${tid}, SSRC: ${report.ssrc}`);
          label = this.ssrcLabelMap[report.ssrc];
          if (!label) {
            console.log(`No label is defined for ssrc = ${report.ssrc}`, this.ssrcLabelMap);
          }
          break;
        }
      }
      if (!label) {
        label = this.trackIdLabelMap[tid];
        if (!label) {
          // if local track, event.transceiver.sender.track may be registered
          if (event.transceiver) {
            if (event.transceiver.mid == "probator") {
              console.log("ignore probator");
              return;
            }
            console.log("event.transceiver.sender.track", event.transceiver.sender.track);
            if (event.transceiver.sender.track != null) {
              label = this.trackIdLabelMap[event.transceiver.sender.track.id];
            }
          }
          if (!label) {
            console.log(`No label is defined for tid = ${tid}`);
            track.stop();
            return;
          }
        }
      }
      let t = this.tracks[QRPCTrack.key(label, track.kind)];
      if (!t) {
        console.log(`No media for label ${label}/${track.kind}`);
        track.stop();
        return;
      }
      if (!t.active) {
        t.track = track;
        t.transceiver = event.transceiver;
        t.stream = event.streams[0];
      }
      const r = t.onopen(t);
      if (r === false || r === null) {
        console.log(`close media by application ${label}/${track.kind}`);
        this.closeMedia(label, track.kind);
        return;
      }
    }
    
    // Listen for state change events
    pc.oniceconnectionstatechange = (event) => {
      console.log("ICE connection state change", pc.iceConnectionState);
    };
    pc.onconnectionstatechange = (event) =>{
      console.log("Connection state change", pc.connectionState);
      switch(pc.connectionState) {
        case "connected":
          // The connection has become fully connected
          break;
        case "disconnected":
        case "failed":
          // One or more transports has terminated unexpectedly or in an error
          this.close();
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
  }
  async #handshake() {
    // generate syscall stream (it also ensures that SDP for data channel is generated)
    this.syscallStream = this.openStream(QRPClient.SYSCALL_STREAM, {
      onmessage: this.#syscallMessageHandler.bind(this)
    });
    // dummy onopen call to generate correct offer of dummy PeerConnection
    if (this.onopen) {
      this.context = await this.onopen();
    }
    // Create new SDP offer
    const offer = await this.pc.createOffer();
    console.log("offer sdp", offer.sdp);
    const oldSdp = this.pc.localDescription;
    // (re)Set local description
    await this.pc.setLocalDescription(offer);

    // gathering mid-label mapping, because it needs to call after sdp is set,
    // setLocalDescription must called before running these codes
    for (const k in this.tracks) {
      const t = this.tracks[k];
      if (!t.active) { continue; }
      this.midLabelMap[t.mid] = t.label;
    }

    //store local ice ufrag/pwd
    this.iceUsername = offer.sdp.match(/a=ice-ufrag:(.*)[\r\n]+/)[1];
    this.icePassword = offer.sdp.match(/a=ice-pwd:(.*)[\r\n]+/)[1];

    let answer;
    if (this.sdpGen < 0) {
      //Do the post request to the WHIP endpoint with the SDP offer
      const fetched = await fetch(this.url, {
        method: "POST",
        body: JSON.stringify({sdp:offer.sdp,cname:this.cname,rtp:this.#rtpPayload()}),
        headers: {
          "Content-Type": "application/json"
        }
      });

      if (!fetched.ok)
        throw new Error(`Request rejected with status ${fetched.status}`)

      //Get the SDP answer
      answer = await fetched.text();
      this.sdpGen++;
    } else {
      const gen = this.sdpGen++;
      const reconnect = this.reconnect;
      try {
        answer = await new Promise((resolve, reject) => {
          const msgid = this.#newMsgId();
          this.syscall("nego", {sdp:offer.sdp,cname:this.cname,gen,rtp:this.#rtpPayload(),msgid});
          this.rpcPromises[msgid] = { resolve, reject };
        });
      } catch (e) {
        console.log("syscall.nego fails:", gen, e.message);
        if (gen === this.sdpGen && reconnect === this.reconnect) {
          console.log("rollback sdp for gen:", this.sdpGen, "reconnect:", this.reconnect);
          // rollback
          this.setLocalDescription(oldSdp.sdp);
        } else {
          console.log(
            "ignore rollback sdp for gen:", gen, "current gen:", this.sdpGen,
            "reconnect:", reconnect, "current reconnect:", this.reconnect
          );
        }
      }
    }

    this.id = answer.match(/a=ice-ufrag:(.*)[\r\n]+/)[1];
    console.log("id", this.id, "answer sdp", answer);

    //And set remote description
    await this.pc.setRemoteDescription({type:"answer",sdp:answer});

    // // re-create RTCPeerConnection
    // this.#clear();
    // this.pc = new RTCPeerConnection();
    // this.pc.setConfiguration({ iceTransportPolicy: 'all' });

    // this.#setupCallbacks(this.pc);
    // this.syscallStream = this.openStream(QRPClient.SYSCALL_STREAM);
    // if (this.onopen) {
    //   this.context = await this.onopen();
    // }
    // // And set remote description
    // await this.pc.setRemoteDescription({type:"offer",sdp:answer});

    // // (re)Set local description
    // const localAnswer = await this.pc.createAnswer();
    // localAnswer.sdp = localAnswer.sdp.replace(/a=ice-options:trickle/, "a=ice-options:trickle ice-lite")
    //   .replace(/a=setup:passive/, `a=setup:active`);
    // console.log("localAnswer", localAnswer);
    // await this.pc.setLocalDescription(localAnswer);
  }
  close() {
    if (!this.pc) {
      // Already stopped
      return
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
    if (this.pc.connectionState != "failed") {
      this.pc.close();
    }
    if (this.cpc) {
      if (this.cpc.connectionState != "failed") {
        this.cpc.close();
      }
      this.cpc = null;
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
      Object.keys(this.trackIdLabelMap).length !== 0 ||
      Object.keys(this.midLabelMap).length !== 0
    ) {
      return {
        ridLabelMap:this.ridLabelMap,
        ridScalabilityModeMap:this.ridScalabilityModeMap,
        trackIdLabelMap:this.trackIdLabelMap,
        midLabelMap:this.midLabelMap,
      };
    }
    return undefined;
  }
  async createMedia(label, {stream, encodings, onopen, onclose}) {
    console.log("createMedia", label, stream, encodings);
    if (!encodings) {
      throw new Error("encoding is mandatory");
    }
    encodings.forEach(e => {
      if (!e.rid) { throw new Error("for each encodings, rid is mandatory"); }
      if (!e.maxBitrate) { throw new Error("for each encodings, maxBitrate is mandatory"); }
      e.scalabilityMode = e.scalabilityMode || QRPClient.DEFAULT_SCALABILITY_MODE;
      this.ridLabelMap[e.rid] = label;
      this.ridScalabilityModeMap[e.rid] = e.scalabilityMode;
    });
    // sort by maxBitrate asc, because server regards the first encoding as the lowest quality,
    // regardless its bitrate
    encodings.sort((a, b) => a.maxBitrate - b.maxBitrate);
    const tracks = {};
    stream.getTracks().forEach(t => {
      this.trackIdLabelMap[t.id] = label
      const track = new QRPCTrack(label, stream, t, encodings, onopen, onclose);
      this.tracks[track.key] = track;
      track.open(this.pc);
      tracks[t.kind] = t;
    });
    if (this.sdpGen >= 0 && !this.mediaHandshaked) {
      // renegotiation
      await this.#handshake();
      this.mediaHandshaked = true;
    }
    return tracks;
  }
  async openMedia(label, {onopen, onclose, audio, video}) {
    const sdp = await new Promise((resolve, reject) => {
      const msgid = this.#newMsgId();
      this.syscall("consume", { label, options: (audio || video) ? { audio, video } : undefined, msgid });
      this.rpcPromises[msgid] = { resolve, reject };
    });
    console.log("openMedia remote offer", sdp);
    const tracks = {};
    for (const kind of ["video", "audio"]) {
      const t = new QRPCTrack(label, null, null, undefined, onopen, onclose);
      const key = QRPCTrack.key(label, kind);
      console.log("openMedia: add track for", key);
      this.tracks[key] = t;
      tracks[kind] = t;
    }
    this.cpc = await this.#createPeerConnection();
    this.#setupCallbacks(this.cpc);
    await this.cpc.setRemoteDescription({type:"offer",sdp});
    const answer = await this.cpc.createAnswer();
    console.log("openMedia local answer", answer.sdp);
    await this.cpc.setLocalDescription(answer);
    return tracks;
  }
  closeMedia(label, kind) {
    const kinds = kind ? [kind] : ["video", "audio"];
    for (const kind of kinds) {
      const key = QRPCTrack.key(label, kind);
      const t = this.tracks[key];
      if (t) {
        t.close();
        delete this.tracks[key];
      }
    }
  }
  // options combines createDataChannel's option and RTCDataChannel event handler (onopen, onclose, onmessage)
  openStream(label, options) {
    if (this.streams[label]) {
      return this.streams[label];
    }
    const s = this.pc.createDataChannel(label, options);
    this.#setupStream(s, options);
    return s;
  }
  closeStream(label) {
    const s = this.streams[label];
    if (!s) {
      console.log(`No stream for label ${label}`);
      return;
    }
    s.close();
    delete this.streams[label];
  }
  syscall(fn, args) {
    this.syscallStream.send(JSON.stringify({fn, args}));
  }
  #setupStream(s, h) {
    s.onopen = (h.onopen && ((event) => {
      const ctx = h.onopen(s, event);
      if (ctx === false || ctx === null) {
        console.log(`close stream by application label=${s.label}`);
        this.closeStream(s.label);
        return;
      } else {
        s.context = ctx;
      }
    })) || ((event) => {});
    s.onclose = (h.onclose && ((event) => {
      h.onclose(s, event);
    })) || ((event) => {});
    s.onmessage = (event) => h.onmessage(s, event);
    this.streams[s.label] = s;
  }
};