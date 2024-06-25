class QRPCMedia {
  constructor(label, stream, type) {
    this.label
    this.stream = stream;
    if (type === "sender") {
      for (const t in this.stream.getTracks()) {
        this.addTrack(t);
      }
    } else {
      this.tracks = {}
    }
  }
  close() {
    Object.keys(this.tracks).forEach(tid => this.tracks[tid].stop());
  }
  addTrack(t) {
    this.tracks[t.trackId] = t;
  }
  // c: QRPCClient
  assoc_tracks(c, label) {
    c.syscall("tlmap", {ids:Object.keys(this.tracks),label});
  }
  assoc_tracks_acked(c, label) {
    const id = c.labelToId(label, true);
    const h = c.mdmap[id];
    this.senders = this.tracks.forEach(t => c.pc.addTrack(t, this.stream));
    if (!h.onopen) {
      return;
    }
    for (const t in this.tracks) {
      const r = h.onopen(this, t);
      if (r === false || r === null) {
        console.log("close media by application", label);
        c.closeMedia(label);
        return;
      }
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
    this.trackLabelMap = {};
    this.labelToId = (label, isMedia) => label;
    this.handle(QRPClient.SYSCALL_STREAM, {
      onmessage: (s, event) => {
        const data = JSON.parse(event.data);
        if (data.fn === "close") {
          console.log("shutdown by server");
          this.close();
        } else if (data.fn === "tlmap") {
          for (const k in data.args.ids) {
            this.trackLabelMap[k] = data.args.label
          }
          this.syscall("tlmap_ack",data.args);
        } else if (data.fn === "tlmap_ack") {
          const m = this.medias[data.args.label];
          if (!m) {
            m.assoc_tracks_acked(this, data.args.label);
          }
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
      console.log("accept stream", s.label);
      const h = this.hdmap[s.label];
      if (!h) {
        console.log("No stream callbacks for label [" + s.label + "]");
        s.close();
        return;
      }
      this.#setupStream(s, h);
    };

    //Listen for media medias
    pc.ontrack = (event) => {
      const tid = event.track.trackId
      const label = this.trackLabelMap[tid];
      if (!label) {
        console.log("No label is defined for trackId = " + tid);
        event.track.stop();
        return;
      }
      const h = this.mdmap[label];
      if (!h) {
        console.log("No handler is defined for label = " + tid);
        event.track.stop();
        return;
      }
      let m = this.media[label];
      if (!m) {
        m = new QRPCMedia(label, event.track.stream, "receiver");
        this.media[label] = m;
      }
      m.addTrack(t);
      const r = h.onopen(m, t);
      if (r === false || r === null) {
        console.log("close media by application", label);
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
      throw new Error("Request rejected with status " + fetched.status)

    //Get the SDP answer
    const answer = await fetched.text();

    console.log("whip answer", answer)

    //Set local description
    await pc.setLocalDescription(offer);

    //store ice ufrag/pwd
    this.iceUsername = offer.sdp.match(/a=ice-ufrag:(.*)\r\n/)[1];
    this.icePassword = offer.sdp.match(/a=ice-pwd:(.*)\r\n/)[1];
    
    //And set remote description
    await pc.setRemoteDescription({type:"answer",sdp: answer});
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
      console.log("attempt reconnect after " + reconnectionWaitMS + "ms");
      setTimeout(() => {
        this.connect();
      }, reconnectionWaitMS);
    } else {
      console.log("no reconnect. bye!");
    }
  }
  handleMedia(handler_id, callbacks) {
    if (this.mdmap[handler_id]) {
      console.log("worker already run for " + handler_id);
      return;
    }
    if (this.mdmap[handler_id]) {
      Object.assign(this.mdmap[handler_id], callbacks);
      return;
    }
    this.mdmap[handler_id] = callbacks;
  }
  openMedia(label, stream) {
    if (this.medias[label]) {
      return this.medias[label];
    }
    const id = this.labelToId(label, true);
    const h = this.mdmap[id];
    if (!h) {
      console.log("No media callbacks for id " + id);
      return null;
    }
    const m = new QRPCMedia(label, stream, "sender");
    m.assoc_tracks(this, label);
    this.medias[label] = m;
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
      console.log("No stream callbacks for id " + id);
      return null;
    }
    const s = this.pc.createDataChannel(label);
    this.#setupStream(s, h);
    return s;
  }
  closeStream(label) {
    const s = this.streams[label];
    if (!s) {
      console.log("No stream for label " + label);
      return;
    }
    s.close();
    this.streams[label] = null;
  }
  syscall(fn, args) {
    this.syscallStream.send(JSON.stringify(fn, args));
  }

  #setupStream(s, h) {
    s.onopen = (h.onopen && ((event) => {
      const ctx = h.onopen(s, event);
      if (ctx === false || ctx === null) {
        console.log("close stream by application", s.label);
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