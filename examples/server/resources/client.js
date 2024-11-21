class QRPCMedia {
  constructor(label, stream, encodings) {
    this.label = label;
    this.stream = stream;
    this.tracks = {};
    this.receivers = {}
    if (encodings) {
      this.encodings = encodings;
      const vtracks = stream.getVideoTracks();
      if (vtracks.length > 0) {
        console.log("stream has", vtracks.length, "video tracks");
        this.addTrack("video", vtracks[0]);
      } else {
        console.log("no video track in stream");
      }
      const atracks = stream.getAudioTracks();
      if (atracks.length > 0) { 
        console.log("stream has", atracks.length, "audio tracks");
        this.addTrack("audio", atracks[0]);
      } else {
        console.log("no audio track in stream");
      }
    }
  }
  // c: QRPClient
  open(c) {
    if (this.tracks.video) {
      this.receivers.video = c.pc.addTransceiver(
        this.tracks.video,
        {direction: 'sendonly', sendEncodings: this.encodings, streams: [this.stream]}
      );
    }
    if (this.tracks.audio) {
      this.receivers.audio = c.pc.addTransceiver(
        this.tracks.audio,
        {direction: 'sendonly', streams: [this.stream]}
      );
    }
  }
  close() {
    for (const k in this.tracks) {
      this.tracks[k].stop();
    }
  }
  addTrack(name, t) {
    if (!this.tracks[name]) {
      this.tracks[name] = t;
    }
  }
  getMidLabelMap(c) {
    for (const k in this.receivers) {
      const r = this.receivers[k];
      c.midLabelMap[r.mid] = this.label;
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
    this.hdmap = {};
    this.mdmap = {};
    this.reconnect = 0;
    this.#clear();
    this.handlerResolver = (c, label, isMedia) => {
      const handler_id = label.indexOf("?") > 0 ? label.split("?")[0] : label;
      return isMedia ? c.mdmap[handler_id] : c.hdmap[handler_id];
    }
    this.handle(QRPClient.SYSCALL_STREAM, {
      onmessage: async (s, event) => {
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
    });
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
    this.rpcPromises[msgid] = undefined;
    return promise;
  }
  async connect() {
    //If already publishing
    if (this.pc) {
      console.log("Already connected");
      return;
    }
    // create dummy peer connection to generate sdp
    const pc = new RTCPeerConnection(); 
    // Store pc object and token
    this.pc = pc;
    this.initIce();
    this.#setupCallbacks(pc);
    await this.#handshake();
  }
  #setupCallbacks(pc) {
    // Listen for data channels
    pc.ondatachannel = (event) => {            
      const s = event.channel;
      console.log(`accept stream ${s.label}`);
      const h = this.hdmap[s.label];
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
            event.track.stop();
            return;
          }
          break;
        } else {
          console.log(`track id: ${event.track.id}, no video track ${report}`);
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
      const h = this.handlerResolver(this, label, true);
      if (!h) {
        console.log(`No handler is defined for label = ${label} (${tid})`);
        track.stop();
        return;
      }
      let m = this.medias[label];
      if (!m) {
        m = new QRPCMedia(label, track.stream);
        this.medias[label] = m;
      }
      m.addTrack(track.kind, track);
      const r = h.onopen(m, track);
      if (r === false || r === null) {
        console.log(`close media by application ${label}`);
        this.closeMedia(label);
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
    this.syscallStream = this.openStream(QRPClient.SYSCALL_STREAM);
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
    for (const k in this.medias) {
      const m = this.medias[k];
      m.getMidLabelMap(this);
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
  handleMedia(handler_name, callbacks) {
    if (this.mdmap[handler_name]) {
      console.log(`worker already run for ${handler_name}`);
      return;
    }
    if (this.mdmap[handler_name]) {
      Object.assign(this.mdmap[handler_name], callbacks);
      return;
    }
    this.mdmap[handler_name] = callbacks;
  }
  async openMedia(label, stream, encodings) {
    if (this.medias[label]) {
      return this.medias[label];
    }
    if (!encodings) {
      throw new Error("encoding is mandatory");
    }
    stream.getTracks().forEach(t => this.trackIdLabelMap[t.id] = label);
    encodings.forEach(e => {
      if (!e.rid) {
        throw new Error("for each encodings, rid is mandatory");
      }
      e.scalabilityMode = e.scalabilityMode || QRPClient.DEFAULT_SCALABILITY_MODE;
      this.ridLabelMap[e.rid] = label;
      this.ridScalabilityModeMap[e.rid] = e.scalabilityMode;
    });
    const m = new QRPCMedia(label, stream, encodings);
    this.medias[label] = m;
    m.open(this);
    if (this.sdpGen >= 0 && !this.mediaHandshaked) {
      // renegotiation
      await this.#handshake();
      this.mediaHandshaked = true;
    }
    return m;
  }
  async consumeMedia(path, options) {
    const sdp = await new Promise((resolve, reject) => {
      const msgid = this.#newMsgId();
      this.syscall("consume", { path, options, msgid });
      this.rpcPromises[msgid] = { resolve, reject };
    });
    console.log("consumeMedia remote offer", sdp);
    this.cpc = new RTCPeerConnection();
    this.#setupCallbacks(this.cpc);    
    this.cpc.setRemoteDescription({type:"offer",sdp});
    const answer = await this.cpc.createAnswer();
    console.log("consumeMedia local answer", answer.sdp);
    this.cpc.setLocalDescription(answer);
  }
  closeMedia(label) {
    const m = this.medias[label];
    if (!m) {
      console.log("No media for label " + label);
      return;
    }
    const h = this.handlerResolver(this, label, true);
    if (h && h.onclose) {
      h.onclose(m);
    }
    m.close();
    this.medias[label] = null;
  }
  handle(handler_name, callbacks) {
    if (typeof callbacks.onmessage !== "function") {
      throw new Error("callbacks.onmessage is mandatory and should be function");
    }
    if (this.hdmap[handler_name]) {
      Object.assign(this.hdmap[handler_name], callbacks);
      return;
    }
    this.hdmap[handler_name] = callbacks;
  }
  openStream(label) {
    if (this.streams[label]) {
      return this.streams[label];
    }
    const h = this.handlerResolver(this, label);
    if (!h) {
      console.log(`No stream callbacks for id ${label}`);
      return null;
    }
    const s = this.pc.createDataChannel(label);
    this.#setupStream(s, h);
    return s;
  }
  closeStream(label) {
    const s = this.streams[label];
    if (!s) {
      console.log(`No stream for label ${label}`);
      return;
    }
    s.close();
    this.streams[label] = null;
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