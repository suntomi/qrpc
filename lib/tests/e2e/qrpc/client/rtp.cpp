#include "qrpc.h"

#include "client.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kMediaName = "e2e-rtp";
constexpr const char* kProducerCname = "qrpc-e2e-producer";
constexpr int kPayloadTypeVp8 = 96;
constexpr int kVideoClockRate = 90000;
constexpr uint32_t kVideoSsrc = 222222;

struct PacketQueue {
  std::mutex mutex;
  std::deque<std::vector<uint8_t>> packets;
  size_t received{0};
  size_t dropped{0};
  size_t max_depth{512};

  void Push(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex);
    if (packets.size() >= max_depth) {
      packets.pop_front();
      dropped++;
    }
    packets.emplace_back(data, data + len);
    received++;
  }

  bool Pop(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(mutex);
    if (packets.empty()) {
      return false;
    }
    out = std::move(packets.front());
    packets.pop_front();
    return true;
  }
};

struct FfmpegProcess {
  pid_t pid{-1};

  bool Start(const std::vector<std::string>& args) {
    pid = fork();
    if (pid < 0) {
      return false;
    }
    if (pid == 0) {
      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      execvp(argv[0], argv.data());
      _exit(127);
    }
    return true;
  }

  void Stop() {
    if (pid <= 0) {
      return;
    }
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
      int status = 0;
      auto r = waitpid(pid, &status, WNOHANG);
      if (r == pid) {
        pid = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    pid = -1;
  }
};

struct UdpReceiver {
  int fd{-1};
  int port{0};
  std::atomic<bool> alive{false};
  std::thread thread;
  PacketQueue* queue{nullptr};

  bool Open(PacketQueue& q) {
    queue = &q;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
      return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      close(fd);
      fd = -1;
      return false;
    }
    socklen_t len = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
      close(fd);
      fd = -1;
      return false;
    }
    port = ntohs(addr.sin_port);
    return true;
  }

  void Start() {
    alive = true;
    thread = std::thread([this]() {
      uint8_t buf[2048];
      while (alive.load()) {
        auto n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
          queue->Push(buf, static_cast<size_t>(n));
        } else if (n < 0 && errno != EINTR) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    });
  }

  void Close() {
    alive = false;
    if (fd >= 0) {
      shutdown(fd, SHUT_RDWR);
    }
    if (thread.joinable()) {
      thread.join();
    }
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
  }
};

struct UdpSender {
  int fd{-1};
  sockaddr_in addr{};

  bool Open(int port) {
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
      return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    return true;
  }

  bool Send(const void* data, size_t len) {
    auto n = sendto(fd, data, len, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return n == static_cast<ssize_t>(len);
  }

  void Close() {
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
  }
};

struct RtpState {
  PacketQueue producer_packets;
  UdpReceiver ffmpeg_rtp_in;
  UdpSender ffmpeg_rtp_out;
  FfmpegProcess ffmpeg_source;
  FfmpegProcess ffmpeg_sink;
  std::vector<uint8_t> current_packet;
  std::string output_path{"/tmp/qrpc-e2e-rtp.webm"};
  std::string sdp_path{"/tmp/qrpc-e2e-rtp.sdp"};
  qrpc_client_t producer_client{nullptr};
  qrpc_client_t watcher_client{nullptr};
  qrpc_conn_t producer_conn{};
  qrpc_conn_t watcher_conn{};
  std::atomic<bool> stop_clients{false};
  std::atomic<bool> producer_opened{false};
  std::atomic<bool> watcher_opened{false};
  std::atomic<bool> media_opened{false};
  std::atomic<size_t> consumed_packets{0};
  std::string error;
};

bool CommandExists(const char* command) {
  std::string cmd = "command -v ";
  cmd += command;
  cmd += " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

bool WriteSdp(const std::string& path, int port) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return false;
  }
  out
    << "v=0\n"
    << "o=- 0 0 IN IP4 127.0.0.1\n"
    << "s=qrpc rtp e2e\n"
    << "c=IN IP4 127.0.0.1\n"
    << "t=0 0\n"
    << "m=video " << port << " RTP/AVP " << kPayloadTypeVp8 << "\n"
    << "a=rtpmap:" << kPayloadTypeVp8 << " VP8/" << kVideoClockRate << "\n"
    << "a=recvonly\n";
  return true;
}

int PickUdpPort() {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return 0;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return 0;
  }
  socklen_t len = sizeof(addr);
  if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    close(fd);
    return 0;
  }
  int port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

qrpc_media_params_t VideoParams() {
  static qrpc_media_encoding_t encoding{
    .max_bitrate = 1000000,
    .scalability_mode = nullptr,
    .ssrc = kVideoSsrc,
    .rtx_ssrc = 0,
    .payload_type = kPayloadTypeVp8,
    .rtx_payload_type = 0,
    .dtx = false,
    .rtx = false,
  };
  auto media = qrpc_media_config();
  return {
    .n_codecs = media.video_cap.n_codecs,
    .codecs = media.video_cap.codecs,
    .n_hdexts = media.video_cap.n_hdexts,
    .hdexts = media.video_cap.hdexts,
    .n_encodings = 1,
    .encodings = &encoding,
  };
}

qrpc_media_produce_config_t ProduceConfig(qrpc_on_media_produce_t source) {
  auto media = qrpc_media_config();
  return {
    .path = kMediaName,
    .audio = {
      .params = media.audio_cap,
      .paused = true,
      .source = qrpc_closure_empty(),
    },
    .video = {
      .params = VideoParams(),
      .paused = false,
      .source = source,
    },
  };
}

qrpc_media_consume_config_t ConsumeConfig() {
  auto media = qrpc_media_config();
  static std::string path = std::string(kProducerCname) + "/" + kMediaName + "/video";
  return {
    .path = path.c_str(),
    .audio = {
      .params = media.audio_cap,
      .paused = true,
    },
    .video = {
      .params = VideoParams(),
      .paused = false,
    },
  };
}

void* OnProduce(void* arg, qrpc_size_t* size, const qrpc_media_produce_context_t*) {
  auto* state = static_cast<RtpState*>(arg);
  if (!state->producer_packets.Pop(state->current_packet)) {
    *size = 0;
    return nullptr;
  }
  *size = state->current_packet.size();
  return state->current_packet.data();
}

bool OnConsume(void* arg, qrpc_media_t, const void* data, qrpc_size_t datalen) {
  auto* state = static_cast<RtpState*>(arg);
  if (state->ffmpeg_rtp_out.Send(data, datalen)) {
    state->consumed_packets++;
    return true;
  }
  state->error = "failed to forward consumed RTP packet to ffmpeg";
  return false;
}

bool OnMediaOpen(void* arg, qrpc_media_t media, void**) {
  auto* state = static_cast<RtpState*>(arg);
  qrpc_on_media_consume_t consume;
  qrpc_closure_init(consume, OnConsume, state);
  qrpc_media_watch(media, consume);
  state->media_opened = true;
  return true;
}

void OnMediaClose(void*, qrpc_media_t) {}

void OnMediaStateChange(void*, qrpc_media_t, const char*, const char*) {}

qrpc_media_handler_t* MediaRouter(void* arg, const char*, qrpc_conn_t) {
  static qrpc_media_handler_t handler{};
  static bool initialized = false;
  if (!initialized) {
    qrpc_closure_init(handler.on_media_open, OnMediaOpen, arg);
    qrpc_closure_init(handler.on_media_close, OnMediaClose, arg);
    qrpc_closure_init(handler.on_media_state_change, OnMediaStateChange, arg);
    initialized = true;
  }
  return &handler;
}

int OnProducerOpen(void* arg, qrpc_conn_t conn, void**) {
  auto* state = static_cast<RtpState*>(arg);
  state->producer_conn = conn;
  auto media = qrpc_media_config();
  media.cname = kProducerCname;
  qrpc_conn_media_init(conn, &media);
  qrpc_on_media_produce_t source;
  qrpc_closure_init(source, OnProduce, state);
  auto produce = ProduceConfig(source);
  qrpc_conn_media_open(conn, &produce);
  state->producer_opened = true;
  return QRPC_OK;
}

int OnWatcherOpen(void* arg, qrpc_conn_t conn, void**) {
  auto* state = static_cast<RtpState*>(arg);
  state->watcher_conn = conn;
  auto media = qrpc_media_config();
  media.cname = "qrpc-e2e-watcher";
  qrpc_conn_media_init(conn, &media);
  auto consume = ConsumeConfig();
  qrpc_conn_media_watch(conn, &consume);
  state->watcher_opened = true;
  return QRPC_OK;
}

qrpc_time_t OnConnClose(void* arg, qrpc_conn_t, const qrpc_close_reason_t* reason, bool) {
  auto* state = static_cast<RtpState*>(arg);
  if (state->error.empty() && reason->code == QRPC_CLOSE_REASON_PROTOCOL) {
    state->error = std::string("protocol error:") + std::string(reason->msg, reason->msglen);
  }
  return qrpc_alarm_stop_rv();
}

void OnConnFinalize(void*, qrpc_conn_t) {}

qrpc_client_t NewClient() {
  auto clconf = qrpc_client_conf();
  clconf.max_nfd = 1024;
  clconf.session_timeout = qrpc_time_sec(30);
  clconf.connect_timeout = qrpc_time_sec(10);
  return qrpc_client_create(&clconf);
}

qrpc_connect_conf_t ConnectConfig(
  qrpc_client_t client,
  RtpState& state,
  qrpc_on_client_conn_open_t_proc on_open
) {
  auto conf = qrpc_connect_conf(client, "localhost", 8888);
  qrpc_closure_init(conf.on_open, on_open, &state);
  qrpc_closure_init(conf.on_close, OnConnClose, &state);
  qrpc_closure_init(conf.on_finalize, OnConnFinalize, &state);
  qrpc_closure_init(conf.media_router, MediaRouter, &state);
  conf.transport.proto = QRPC_TRANSPORT_WEBRTC;
  conf.transport.webrtc.rtp = {
    .initial_outgoing_bitrate = 10000000,
    .max_outgoing_bitrate = 0,
    .max_incoming_bitrate = 10000000,
    .min_outgoing_bitrate = 0,
  };
  conf.transport.webrtc.params.max_outgoing_stream_size = 32;
  conf.transport.webrtc.params.initial_incoming_stream_size = 32;
  conf.transport.webrtc.params.send_buffer_size = 256 * 1024;
  conf.transport.webrtc.params.session_timeout = qrpc_time_sec(15);
  conf.transport.webrtc.params.connection_timeout = qrpc_time_sec(60);
  conf.transport.webrtc.params.shutdown_timeout = qrpc_time_sec(3);
  conf.transport.webrtc.params.http_timeout = qrpc_time_sec(5);
  conf.transport.webrtc.params.consent_check_interval = qrpc_time_sec(10);
  conf.transport.webrtc.params.fingerprint_algorithm = "sha-256";
  conf.ep.webrtc = {
    .ip = nullptr,
    .path = "/qrpc",
    .in6 = false,
    .proto = QRPC_WEBRTC_ENDPOINT_PROTOCOL_ALL,
  };
  return conf;
}

void RunClient(
  RtpState* state,
  qrpc_client_t RtpState::*client_member,
  qrpc_on_client_conn_open_t_proc on_open
) {
  auto client = NewClient();
  state->*client_member = client;
  auto conf = ConnectConfig(client, *state, on_open);
  qrpc_client_connect(client, &conf);
  while (!state->stop_clients.load() && state->error.empty()) {
    qrpc_client_poll(client);
  }
  qrpc_client_destroy(client);
  state->*client_member = nullptr;
}

bool StartFfmpeg(RtpState& state) {
  const char* output = std::getenv("QRPC_E2E_RTP_OUTPUT");
  if (output != nullptr && output[0] != '\0') {
    state.output_path = output;
  }
  const char* sdp = std::getenv("QRPC_E2E_RTP_SDP");
  if (sdp != nullptr && sdp[0] != '\0') {
    state.sdp_path = sdp;
  }
  if (!CommandExists("ffmpeg")) {
    state.error = "ffmpeg not found";
    return false;
  }
  if (!state.ffmpeg_rtp_in.Open(state.producer_packets)) {
    state.error = "failed to open ffmpeg source RTP UDP receiver";
    return false;
  }
  int sink_port = PickUdpPort();
  if (sink_port == 0 || !WriteSdp(state.sdp_path, sink_port)) {
    state.error = "failed to prepare ffmpeg sink SDP";
    return false;
  }
  if (!state.ffmpeg_rtp_out.Open(sink_port)) {
    state.error = "failed to open ffmpeg sink RTP UDP sender";
    return false;
  }
  state.ffmpeg_rtp_in.Start();
  if (!state.ffmpeg_sink.Start({
    "ffmpeg", "-hide_banner", "-loglevel", "warning", "-y",
    "-protocol_whitelist", "file,udp,rtp",
    "-fflags", "+genpts",
    "-i", state.sdp_path,
    "-t", "5",
    "-c:v", "copy",
    state.output_path,
  })) {
    state.error = "failed to start ffmpeg sink";
    return false;
  }
  std::string rtp_url = "rtp://127.0.0.1:" + std::to_string(state.ffmpeg_rtp_in.port) + "?pkt_size=1200";
  if (!state.ffmpeg_source.Start({
    "ffmpeg", "-hide_banner", "-loglevel", "warning",
    "-re",
    "-f", "lavfi",
    "-i", "testsrc=size=160x120:rate=15",
    "-an",
    "-c:v", "libvpx",
    "-deadline", "realtime",
    "-cpu-used", "8",
    "-payload_type", std::to_string(kPayloadTypeVp8),
    "-ssrc", std::to_string(kVideoSsrc),
    "-f", "rtp",
    rtp_url,
  })) {
    state.error = "failed to start ffmpeg source";
    return false;
  }
  return true;
}

bool OutputLooksValid(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

} // namespace

int test_rtp_client() {
  RtpState state;
  if (!StartFfmpeg(state)) {
    QLOG(ERROR, "rtp test setup failed", { QLOG_STR("error", state.error.c_str()) });
    state.ffmpeg_source.Stop();
    state.ffmpeg_sink.Stop();
    state.ffmpeg_rtp_in.Close();
    state.ffmpeg_rtp_out.Close();
    return 1;
  }

  std::thread producer_thread(RunClient, &state, &RtpState::producer_client, OnProducerOpen);
  std::thread watcher_thread(RunClient, &state, &RtpState::watcher_client, OnWatcherOpen);

  auto deadline = qrpc_time_now() + qrpc_time_sec(12);
  while (state.error.empty() && qrpc_time_now() < deadline) {
    if (state.media_opened.load() && state.consumed_packets.load() >= 30) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  state.stop_clients = true;
  if (producer_thread.joinable()) {
    producer_thread.join();
  }
  if (watcher_thread.joinable()) {
    watcher_thread.join();
  }
  state.ffmpeg_source.Stop();
  state.ffmpeg_sink.Stop();
  state.ffmpeg_rtp_in.Close();
  state.ffmpeg_rtp_out.Close();

  if (state.error.empty() && state.consumed_packets.load() == 0) {
    state.error = "no RTP packets consumed";
  }
  if (state.error.empty() && !OutputLooksValid(state.output_path)) {
    state.error = "ffmpeg did not create RTP output video";
  }
  if (!state.error.empty()) {
    QLOG(ERROR, "rtp client failed", { QLOG_STR("error", state.error.c_str()) });
    return 1;
  }
  QLOG(INFO, "rtp client succeeded", {
    QLOG_INT("source_packets", static_cast<int64_t>(state.producer_packets.received)),
    QLOG_INT("consumed_packets", static_cast<int64_t>(state.consumed_packets.load())),
    QLOG_STR("output", state.output_path.c_str())
  });
  return 0;
}
