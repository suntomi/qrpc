class QRPCMedia {
  constructor(label, stream, encodings) {
    this.label = label;
    this.stream = stream;
    this.tracks = {};
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
      c.pc.addTransceiver(
        this.tracks.video,
        {direction: 'sendonly', sendEncodings: this.encodings, streams: [this.stream]}
      );
    }
    if (this.tracks.audio) {
      c.pc.addTransceiver(
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
}
class QRPClient {
  static SYSCALL_STREAM = "$syscall";
  constructor(url) {
    this.url = url;
    this.streams = {};
    this.medias = {};
    this.hdmap = {};
    this.mdmap = {};
    this.trackIdLabelMap = {}; // trackId -> label
    this.ridLabelMap = {}; // RTP stream id -> label
    this.sdpOfferMap = {}; // sdpGen -> sdp offer
    this.sdpGen = -1;
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
          this.syscall("nego_ack",{label:data.args.label,sdp:answer.sdp,gen:this.sdpGen});
        } else if (data.fn === "nego_ack") {
          const offer = this.sdpOfferMap[data.args.gen];
          if (!offer) {
            console.log(`sdp offer for gen:${data.args.gen} does not exist`);
            return;
          }
          this.sdpOfferMap[data.args.gen] = null;
          if (this.sdpGen > data.args.gen) {
            console.log(`old sdp anwser for ${data.args.gen} ignored`);
            return;
          } else if (this.sdpGen < data.args.gen) {
            console.log(`future sdp anwser for ${data.args.gen} ignored`);
            return;
          }
          if (data.args.error) {
            console.log(`invalid sdp ${offer.sdp}: ${data.args.error}`);
            return;
          } else {
            console.log("answer sdp", data.args.sdp)
            await this.#setupSdp(offer, data.args.sdp);
          }
        }
      }
    });
  }
  initIce() {
    //Ice properties
    this.iceUsername = null;
    this.icePassword = null;
    //Pending candidadtes
    this.candidates = [];
    this.endOfcandidates = false;
  }
  async connect() {
    //If already publishing
    if (this.pc) {
      console.log("Already connected");
      return;
    }

    const pc = new RTCPeerConnection(); 
    //Store pc object and token
    this.pc = pc;
    this.initIce();

    //Listen for data channels
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

    //Listen addition of media tracks
    pc.ontrack = (event) => {
      console.log("ontrack", event);
      const t = event.track;
      const tid = t.id;
      let label = this.trackIdLabelMap[tid];
      if (!label) {
        // if local track, event.transceiver.sender.track may be registered
        if (event.transceiver) {
          label = this.trackIdLabelMap[event.transceiver.sender.track.id];
        }
        if (!label) {
          console.log(`No label is defined for tid =${tid}`);
          event.track.stop();
          return;
        }
      }
      const h = this.handlerResolver(this, label, true);
      if (!h) {
        console.log(`No handler is defined for label = ${label} (${tid})`);
        event.track.stop();
        return;
      }
      let m = this.medias[label];
      if (!m) {
        m = new QRPCMedia(label, t.stream);
        this.medias[label] = m;
      }
      m.addTrack(t.kind, t);
      const r = h.onopen(m, t);
      if (r === false || r === null) {
        console.log(`close media by application ${label}`);
        this.closeMedia(label);
        return;
      }
    }
    
    //Listen for state change events
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

    //Listen for candidates
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

    // generate syscall stream
    this.syscallStream = this.openStream(QRPClient.SYSCALL_STREAM);

    if (this.onopen) {
      this.context = await this.onopen();
    } else {
      throw new Error("must set onopen callback and open initial stream in it");
    }

    //Create SDP offer
    const offer = await pc.createOffer();

    //Request headers
    const headers = {
      "Content-Type": "application/sdp"
    };

    console.log("offer sdp", offer.sdp);

    //Do the post request to the WHIP endpoint with the SDP offer
    const fetched = await fetch(this.url, {
      method: "POST",
      body: JSON.stringify({
        sdp:offer.sdp,
        ridLabelMap:this.ridLabelMap,
        trackIdLabelMap:this.trackIdLabelMap,
      }),
      headers
    });

    if (!fetched.ok)
      throw new Error(`Request rejected with status ${fetched.status}`)

    //Get the SDP answer
    const answer = await fetched.text();

    console.log("answer sdp", answer)

    await this.#setupSdp(offer, answer);

    this.sdpGen++;
  }
  async #setupSdp(offer, answer_sdp) {
    //Set local description
    await this.pc.setLocalDescription(offer);

    //store ice ufrag/pwd
    this.iceUsername = offer.sdp.match(/a=ice-ufrag:(.*)\r\n/)[1];
    this.icePassword = offer.sdp.match(/a=ice-pwd:(.*)\r\n/)[1];
    
    //And set remote description
    await this.pc.setRemoteDescription({type:"answer",sdp:answer_sdp});
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
    this.streams = {};
    if (reconnectionWaitMS > 0) {
      console.log(`attempt reconnect after ${reconnectionWaitMS} ms`);
      setTimeout(() => {
        this.connect();
      }, reconnectionWaitMS);
    } else {
      console.log("no reconnect. bye!");
    }
  }
  async renegotiation() {
    if (this.sdpGen < 0) {
      console.log("QRPClient yet initialized: wait for connect() calling");
      return;
    }
    this.sdpGen++;
    const offer = await this.pc.createOffer();
    this.sdpOfferMap[this.sdpGen] = offer;
    console.log("offer sdp", offer.sdp);
    this.syscall("nego", {
      sdp:offer.sdp,gen:this.sdpGen,
      ridLabelMap:this.ridLabelMap,
      trackIdLabelMap:this.trackIdLabelMap,
    });
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
    encodings.forEach(e => { this.ridLabelMap[e.rid] = label; });
    const m = new QRPCMedia(label, stream, encodings);
    this.medias[label] = m;
    m.open(this);
    await this.renegotiation();
    return m;
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