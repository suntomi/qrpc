class QRPCMedia {
  constructor(label, stream, encodings) {
    this.label = label;
    this.stream = stream;
    this.tracks = {};
    if (this.encoding) {
      this.encodings = encodings;
      for (const e of encodings) {
        e.rid = `${e.rid}@${label}`
      }
      const vtracks = stream.getVideoTracks();
      if (vtracks.length > 0) {
        this.addTrack("video", vtracks[0]);
      }
      const atracks = stream.getAudioTracks();
      if (atracks.length > 0) { 
        this.addTrack("audio", atracks[0]);
      }
    }
  }
  // c: QRPClient
  open(c, h) {
    if (this.tracks.video) {
      c.pc.addTrancsceiver(
        this.tracks.video,
        {direction: 'sendonly', sendEncodings: this.encodings, streams: [this.stream]}
      );
      const r = h.onopen(this, this.tracks.video);
      if (r === false || r === null) {
        console.log(`close video by application ${label}`);
        return false;
      }  
    }
    if (this.tracks.audio) {
      c.pc.addTrancsceiver(
        this.tracks.audio,
        {direction: 'sendonly', streams: [this.stream]}
      );
      const r = h.onopen(this, this.tracks.audio);
      if (r === false || r === null) {
        console.log(`close audio by application ${label}`);
        return false;
      }
    }
    return true;
  }
  close() {
    for (const k in this.tracks) {
      this.tracks[k].stop();
    }
  }
  addTrack(name, t) {
    this.tracks[name] = t;
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
    this.sdpOfferMap = {}; // sdpGen -> sdp offer
    this.sdpGen = -1;
    this.labelToId = (label, isMedia) => label;
    this.handle(QRPClient.SYSCALL_STREAM, {
      onmessage: async (s, event) => {
        const data = JSON.parse(event.data);
        if (data.fn === "close") {
          console.log("shutdown by server");
          this.close();
        } else if (data.fn === "nego") {
          // our library basically exchange media stream via QRPC server
          // at least, we will have to implement WHIP endpoint of QRPC server for first exchanging SDP between peers.
          console.log("should not receive nego from server or other peer");
          await this.pc.setRemoteDescription({type:"offer",sdp:data.args.sdp});
          const answer = await this.pc.createAnswer();
          await this.pc.setLocalDescription(answer);
          this.syscall("nego_ack",{label:data.args.label,sdp:answer.sdp,gen:this.sdpGen});
        } else if (data.fn === "nego_ack") {
          const offer = this.sdpOfferMap[data.args.gen];
          this.sdpOfferMap[data.args.gen] = null;
          if (this.sdpGen > data.args.gen) {
            console.log(`old sdp anwser for ${data.args.gen} ignored`);
            return;
          } else if (this.sdpGen < data.args.gen) {
            console.log(`future sdp anwser for ${data.args.gen} ignored`);
            return;
          }
          await this.#setupSdp(offer, data.args.sdp);
        }
      }
    });
    this.syscallStream = this.openStream(QRPClient.SYSCALL_STREAM);
  }
  init() {
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
    this.init();

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
      const params = event.receiver.getParameters();
      if (!parameters.encodings || parameters.encodings.length <= 0) {
        console.log("no encodings in track");
        event.track.stop();
        return;
      }
      const rid = parameters.encodings[0].rid;
      const parsed = rid.split("@");
      if (parsed.length < 2) {
        console.log("invalid rid format");
        event.track.stop();
        return;
      }
      const label = parsed[1];
      const id = this.labelToId(label, true);
      const h = this.mdmap[id];
      if (!h) {
        console.log(`No handler is defined for label = ${label} (${id})`);
        event.track.stop();
        return;
      }
      const t = event.track;
      let m = this.media[label];
      if (!m) {
        m = new QRPCMedia(label, t.stream);
        this.media[label] = m;
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

    console.log("sdp", offer.sdp);

    //Do the post request to the WHIP endpoint with the SDP offer
    const fetched = await fetch(this.url, {
      method: "POST",
      body: offer.sdp,
      headers
    });

    if (!fetched.ok)
      throw new Error(`Request rejected with status ${fetched.status}`)

    //Get the SDP answer
    const answer = await fetched.text();

    console.log("whip answer", answer)

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
  async renegotiation(label) {
    if (this.sdpGen < 0) {
      console.log("QRPClient yet initialized: wait for connect() calling");
      return;
    }
    this.sdpGen++;
    const offer = await this.pc.createOffer();
    this.sdpOfferMap[this.sdpGen] = offer;
    this.syscall("nego", {label,sdp:offer.sdp,gen:this.sdpGen});
  }
  handleMedia(handler_id, callbacks) {
    if (this.mdmap[handler_id]) {
      console.log(`worker already run for ${handler_id}`);
      return;
    }
    if (this.mdmap[handler_id]) {
      Object.assign(this.mdmap[handler_id], callbacks);
      return;
    }
    this.mdmap[handler_id] = callbacks;
  }
  async openMedia(label, stream, encoding) {
    if (this.medias[label]) {
      return this.medias[label];
    }
    const m = new QRPCMedia(label, stream, encoding);
    this.medias[label] = m;
    const id = this.labelToId(label);
    const h = this.mdmap[id];
    if (!h) {
      throw new Error(`no handler for label ${label} (${id})`);
    }
    if (m.open(this, h)) {
      await this.renegotiation(label);
    } else {
      this.closeMedia(label); 
    }
    return m;
  }
  closeMedia(label) {
    const m = this.medias[label];
    if (!m) {
      console.log("No media for label " + label);
      return;
    }
    const id = this.labelToId(label);
    const h = this.mdmap[id];
    if (h && h.onclose) {
      h.onclose(m);
    }
    m.close();
    this.medias[label] = null;
  }
  handle(handler_id, callbacks) {
    if (typeof callbacks.onmessage !== "function") {
      throw new Error("onmessage is mandatory and should be function");
    }
    if (this.hdmap[handler_id]) {
      Object.assign(this.hdmap[handler_id], callbacks);
      return;
    }
    this.hdmap[handler_id] = callbacks;
  }
  openStream(label) {
    if (this.streams[label]) {
      return this.streams[label];
    }
    const id = this.labelToId(label);
    const h = this.hdmap[id];
    if (!h) {
      console.log(`No stream callbacks for id ${id}`);
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