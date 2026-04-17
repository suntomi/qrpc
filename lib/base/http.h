#pragma once

#include <functional>
#include <string>
#include <map>
#include <cstdlib>
#include <regex>
#include "base/defs.h"
#include "base/session.h"
#include "base/string.h"
#include "base/crypto.h"

namespace base {
    /****** HTTP status codes *******/
    typedef enum
    {
        HRC_ERROR = -1,         /* An error response from httpXxxx() */
        
        HRC_CONTINUE = 100,         /* Everything OK, keep going... */
        HRC_SWITCHING_PROTOCOLS,        /* HRC upgrade to TLS/SSL */
        
        HRC_OK = 200,           /* OPTIONS/GET/HEAD/POST/TRACE command was successful */
        HRC_CREATED,                /* PUT command was successful */
        HRC_ACCEPTED,           /* DELETE command was successful */
        HRC_NOT_AUTHORITATIVE,      /* Information isn't authoritative */
        HRC_NO_CONTENT,         /* Successful command, no new data */
        HRC_RESET_CONTENT,          /* Content was reset/recreated */
        HRC_PARTIAL_CONTENT,            /* Only a partial file was recieved/sent */
        
        HRC_MULTIPLE_CHOICES = 300,     /* Multiple files match request */
        HRC_MOVED_PERMANENTLY,      /* Document has moved permanently */
        HRC_MOVED_TEMPORARILY,      /* Document has moved temporarily */
        HRC_SEE_OTHER,          /* See this other link... */
        HRC_NOT_MODIFIED,           /* File not modified */
        HRC_USE_PROXY,          /* Must use a proxy to access this URI */
        
        HRC_BAD_REQUEST = 400,      /* Bad request */
        HRC_UNAUTHORIZED,           /* Unauthorized to access host */
        HRC_PAYMENT_REQUIRED,       /* Payment required */
        HRC_FORBIDDEN,          /* Forbidden to access this URI */
        HRC_NOT_FOUND,          /* URI was not found */
        HRC_METHOD_NOT_ALLOWED,     /* Method is not allowed */
        HRC_NOT_ACCEPTABLE,         /* Not Acceptable */
        HRC_PROXY_AUTHENTICATION,       /* Proxy Authentication is Required */
        HRC_REQUEST_TIMEOUT,            /* Request timed out */
        HRC_CONFLICT,           /* Request is self-conflicting */
        HRC_GONE,               /* TcpServer has gone away */
        HRC_LENGTH_REQUIRED,            /* A content length or encoding is required */
        HRC_PRECONDITION,           /* Precondition failed */
        HRC_REQUEST_TOO_LARGE,      /* Request entity too large */
        HRC_URI_TOO_LONG,           /* URI too long */
        HRC_UNSUPPORTED_MEDIATYPE,      /* The requested media type is unsupported */
        HRC_REQUESTED_RANGE,            /* The requested range is not satisfiable */
        HRC_EXPECTATION_FAILED,     /* The expectation given in an Expect header field was not met */
        HRC_UPGRADE_REQUIRED = 426,     /* Upgrade to SSL/TLS required */
        
        HRC_SERVER_ERROR = 500,     /* Internal server error */
        HRC_NOT_IMPLEMENTED,            /* Feature not implemented */
        HRC_BAD_GATEWAY,            /* Bad gateway */
        HRC_SERVICE_UNAVAILABLE,        /* Service is unavailable */
        HRC_GATEWAY_TIMEOUT,            /* Gateway connection timed out */
        HRC_NOT_SUPPORTED           /* HRC version not supported */
    } http_result_code_t;


    /******* HttpFSM *******/
    class HttpFSM {
    public:
        typedef http_result_code_t result_code;
        enum state { /* http fsm state */
            state_invalid,
            /* recv state */
            state_recv_header,
            state_recv_body,
            state_recv_body_nochunk,
            state_recv_bodylen,
            state_recv_footer,
            state_recv_comment,
            state_recv_finish,
            /* upgrade to websocket */
            state_websocket_establish,
            /* response pending */
            state_response_pending,
            /* error */
            state_error = -1,
        };
        enum {
            version_1_0 = 10,
            version_1_1 = 11,
        };
        static const uint16_t lflf = 0x0a0a;
        static const uint16_t crlf = 0x0d0a;
        static const uint32_t crlfcrlf = 0x0d0a0d0a;
        static const int MAX_HEADER = 64;
    protected:
        struct context {
            uint8_t     method, version, n_hd, padd;
            int16_t     state, res;
            const char  *hd[MAX_HEADER], *bd;
            uint32_t        bl;
            uint16_t        hl[MAX_HEADER];
        }   m_ctx;
        uint32_t m_max, m_len;
        const char *m_buf;
        char *m_p{nullptr};
    public:
        HttpFSM() {}
        ~HttpFSM() { if (m_p != nullptr) { std::free(m_p); } }
        void    move_from(HttpFSM &fsm) {
            m_ctx = fsm.m_ctx;
            m_max = fsm.m_max;
            m_len = fsm.m_len;
            m_buf = fsm.m_buf;
            m_p = fsm.m_p;
            fsm.m_p = nullptr;
        }
        state   append(const char *b, int bl);
        void    reset(uint32_t chunk_size);
    public:
        void    set_state(state s) { m_ctx.state = s; }
        state   get_state() const { return (state)m_ctx.state; }
        bool    error() const { return get_state() == state_error; }
        void    setrc(result_code rc) { m_ctx.res = (int16_t)rc; }
        void    setrc_from_close_reason(int reason);
    public: /* for processing reply */
        int         version() const { return m_ctx.version; }
        int         hdrlen() const { return m_ctx.n_hd; }
        const char  *hdr(int idx) const { return (idx < hdrlen()) ? m_ctx.hd[idx] : nullptr; }
        char        *hdrstr(const char *key, char *b, int l, int *outlen = nullptr) const;
        bool        hashdr(const char *key) {
            char tok[256];
            return hdrstr(key, tok, sizeof(tok)) != nullptr;
        }
        bool        hdrint(const char *key, int &out) const;
        bool        accept(const char *mime_type) const {
            return hdr_contains("Accept", mime_type);
        }
        bool        accept_encoding(const char *encoding) const {
            return hdr_contains("Accept-Encoding", encoding);
        }
        bool        hdr_contains(const char *header_name, const char *content) const;
        const char  *bodyptr() const { return m_ctx.bd; }
        std::string body() const { return std::string(m_ctx.bd, m_ctx.bl); }
        result_code     rc() const { return (result_code)m_ctx.res; }
        int         bodylen() const { return m_ctx.bl; }
        const char *url(char *b, int l, size_t *p_out = nullptr);
    public: /* util */
        static bool atoi(const char* str, int *i, size_t max);
        static bool htoi(const char* str, int *i, size_t max);
    protected: /* receiving */
        state   recv_header();
        state   recv_body_nochunk();
        state   recv_body();
        state   recv_bodylen();
        state   recv_footer();
        state   recv_comment();
        state   recv_ws_frame();
    protected:
        int     recv_lflf() const;
        int     recv_lf() const;
        char    *current() { return m_p + m_len; }
        const char *current() const { return m_p + m_len; }
        context &recvctx() { return m_ctx; }
        context &sendctx() { return m_ctx; }
        result_code putrc();
    };


    /******* HttpServer *******/
    class HttpProtocol {
    public:
        struct Header {
            const char *key;
            const char *val;
        };
        virtual ~HttpProtocol() = default;
        int Request(const char *method, const char *path, 
            Header *h = nullptr, size_t hsz = 0, const char *body = nullptr, size_t bsz = 0) {
            char buffer[4096];
            return WriteCommon(
                buffer, snprintf(buffer, sizeof(buffer), "%s %s HTTP/1.1\r\n", method, path),
                h, hsz, body, bsz
            );
        }
        int Respond(http_result_code_t rc, Header *h, size_t hsz, const char *body, size_t bsz) {
            char buffer[256];
            return WriteCommon(
                buffer, snprintf(buffer, sizeof(buffer), "HTTP/1.1 %d\r\n", rc),
                h, hsz, body, bsz
            );
        }
        inline int WriteCommon(const char *first_line, size_t first_line_size,
            Header *h, size_t hsz, const char *body, size_t bsz) {
            // +2 for status line and body
            const char *ptrs[hsz + 3];
            char buffers[hsz][1024];
            size_t sizes[hsz + 3];
            sizes[0] = first_line_size;
            ptrs[0] = first_line;
            for (size_t i = 1; i <= hsz; i++) {
                ptrs[i] = buffers[i - 1];
                sizes[i] = snprintf(
                    buffers[i - 1], sizeof(buffers[i - 1]), "%s: %s\r\n",
                    h[i - 1].key, h[i - 1].val
                );
            }
            sizes[hsz + 1] = 2;
            ptrs[hsz + 1] = "\r\n";
            if (body != nullptr) {
                ptrs[hsz + 2] = body;
                sizes[hsz + 2] = bsz;
                return OnSendPayload(ptrs, sizes, hsz + 3);
            } else {
                return OnSendPayload(ptrs, sizes, hsz + 2);
            }
        }
        template<class... Args>
        int Error(http_result_code_t rc, const char *fmt, const Args... args) {
            char buffer[1024];
            DISABLE_FORMAT_SECURITY_WARNING_PUSH
            size_t len = snprintf(buffer, sizeof(buffer), fmt, args...);
            DISABLE_FORMAT_SECURITY_WARNING_POP
            std::string lenstr = std::to_string(len);
            Header h[] = {
                {.key = "Content-Type", .val = "text/plain"},
                {.key = "Content-Length", .val = lenstr.c_str()},
            };
            return Respond(rc, h, 2, buffer, len);
        }
        template<class... Args>
        int NotFound(const char *fmt, const Args... args) {
            return Error(HRC_NOT_FOUND, fmt, args...);
        }
        template<class... Args>
        int BadRequest(const char *fmt, const Args... args) {
            return Error(HRC_BAD_REQUEST, fmt, args...);
        }
        template<class... Args>
        int Unavailable(const char *fmt, const Args... args) {
            return Error(HRC_SERVICE_UNAVAILABLE, fmt, args...);
        }
        template<class... Args>
        int ServerError(const char *fmt, const Args... args) {
            return Error(HRC_SERVER_ERROR, fmt, args...);
        }
        int ProcessRead(HttpFSM &fsm, const char *p, size_t sz);
    protected:
        virtual int OnSendPayload(const char *pp[], size_t *psz, size_t sz) = 0;
        virtual int OnFinishRead() = 0;
    };

    template <class SessionBase>
    class HttpSessionT : public SessionBase, public HttpProtocol {
    public:
        using Self = HttpSessionT<SessionBase>;
        using CloseReason = typename SessionBase::CloseReason;
        using Callback = std::function<SessionBase *(Self&)>;
    public:
        HttpSessionT(SessionFactory &f, Fd fd, const Address &addr) : SessionBase(f, fd, addr) {
            fsm_.reset(1024);
        }
        ~HttpSessionT() override {}
        const HttpFSM &req() const { return fsm_; }
        const HttpFSM &fsm() const { return fsm_; }
        HttpFSM &fsm() { return fsm_; }
        virtual Callback &callback() = 0;
        int OnRead(const char *p, size_t sz) override {
            return HttpProtocol::ProcessRead(fsm_, p, sz);
        }
        int Send(const char *p, size_t sz) override {
            DIE("Send does not supported. use HttpSession::Write instead");
            return QRPC_ENOTSUPPORT;
        }
    protected:
        int OnSendPayload(const char *pp[], size_t *psz, size_t sz) override {
            return this->Writev(pp, psz, sz);
        }
        int OnFinishRead() override {
            auto newsession = callback()(*this);
            if (newsession != nullptr) {
                ASSERT(newsession->fd() == this->fd());
                if (newsession == this) {
                    fsm_.set_state(HttpFSM::state_response_pending);
                    // session does not closed here (deferred).
                    // callbacked module should cleanup connection after response is sent,
                    // by calling Close(...)
                    return QRPC_OK; // not close conection
                } else {
                    // fd is migrate to other session. eg WebSocket.
                    // need to delete this. done by returning QRPC_EGOAWAY below
                    this->MigrateTo(&type::cast_or_die<SessionBase>(*newsession));
                }
            }
            return QRPC_EGOAWAY; // close connection
        }
    private:
        HttpFSM fsm_;
    };
    typedef HttpSessionT<TcpClientSession> HttpClientSession;
    typedef HttpSessionT<TcpListenerSession> HttpListenerSession;


    /******* HttpSessionFactory *******/
    class HttpClient : public TcpClient {
    public:
        typedef HttpClientSession::CloseReason CloseReason;
        class Processor {
        public:
            typedef HttpClientSession::CloseReason CloseReason;
            virtual ~Processor() {}
            virtual TcpClientSession *HandleResponse(HttpClientSession &s) = 0;
            virtual int SendRequest(HttpClientSession &s) = 0;
            virtual void HandleClose(HttpClientSession &s, const CloseReason &r) = 0;
        };
        class Session : public HttpClientSession {
        public:
            Session(
                HttpClient &c, Fd fd, const Address &a, Processor *p
            ) : HttpClientSession(c, fd, a), processor_(p), cb_(
                [this](HttpClientSession &s){ return processor_->HandleResponse(s); }
            ) {}
            Callback &callback() override { return cb_; }
            int OnConnect() override { return processor_->SendRequest(*this); }
            qrpc_time_t OnShutdown() override {
                processor_->HandleClose(*this, close_reason());
                return 0;
            }
        private:
            std::unique_ptr<Processor> processor_;
            Callback cb_;
        };
    public:
        HttpClient(Loop &l, Resolver &r) : HttpClient(l, r, std::nullopt) {}
        // https://superuser.com/a/1271864 says chrome timeout is 300s
        HttpClient(Loop &l, Resolver &r, const MaybeCertPair &cp) :
            TcpClient(l, r, qrpc_time_sec(300)) {
            static_cast<void>(cp);
        }
        bool Connect(const std::string &host, int port, Processor *p, const ClientConnectOptions &opts) {
            return TcpClient::Connect(host, port, [this, p](Fd fd, const Address &addr) {
                return new Session(*this, fd, addr, p);
            }, opts);
        }
        bool Connect(const std::string &host, int port, Processor *p) {
            return Connect(host, port, p, ClientConnectOptions());
        }
    };
    class AdhocHttpClient : public HttpClient {
    public:
        typedef std::function<int (HttpClientSession &)> Sender;
        typedef std::function<TcpClientSession *(HttpClientSession &)> Receiver;
        typedef std::function<void (HttpClientSession &, const CloseReason &)> Closer;
        class Processor : public HttpClient::Processor {
        public:
            Processor(Sender &&scb, Receiver &&rcb, Closer &&ccb) :
                scb_(std::move(scb)), rcb_(std::move(rcb)), ccb_(std::move(ccb)) {}
            Processor(Sender &&scb, Receiver &&rcb) : 
                Processor(std::move(scb), std::move(rcb), Closer(Nop())) {}
            TcpClientSession *HandleResponse(HttpClientSession &s) override { return rcb_(s); }
            int SendRequest(HttpClientSession &s) override { return scb_(s); }
            void HandleClose(HttpClientSession &s, const CloseReason &r) override { ccb_(s, r); }
        public:
            struct Nop {
                void operator()(HttpClientSession &, const CloseReason &) {}
            };
        private:
            Sender scb_;
            Receiver rcb_;
            Closer ccb_;
        };
    public:
        AdhocHttpClient(Loop &l, Resolver &r, const MaybeCertPair &cp = std::nullopt) : HttpClient(l, r, cp) {}
        bool Connect(const std::string &host, int port, Sender &&scb, Receiver &&rcb) {
            return HttpClient::Connect(host, port, new Processor(std::move(scb), std::move(rcb)));
        }
    };


    /******* HttpListener *******/
    class HttpListener : public TcpListener {
    public:
        typedef HttpListenerSession::Callback Callback;
        class Session : public HttpListenerSession {
        public:
            Session(HttpListener &l, Fd fd, const Address &a) : HttpListenerSession(l, fd, a) {}
            HttpListener &listener() { return factory().to<HttpListener>(); }
            Callback &callback() override { return listener().cb(); }
        };
    public:
        HttpListener(Loop &l, Config c = Config::Default()) : TcpListener(l, [this](Fd fd, const Address &a) {
            return new Session(*this, fd, a);
        }, c) {}
        ~HttpListener() {}
        Callback &cb() { return callback_; }
        bool Listen(int port, const Callback &cb) {
            callback_ = cb;
            return TcpListener::Listen(port);
        }
    protected:
        Callback callback_;
    };
    typedef HttpProtocol::Header HttpHeader;


    /******* WebSocketSession *******/
    class WebSocketFSM;
    class WebSocketProtocol {
    public:
        /* web socket frame struct */
        /*---------------------------------------------------------------------------
            0                   1                   2                   3
            0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
            +-+-+-+-+-------+-+-------------+-------------------------------+
            |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
            |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
            |N|V|V|V|       |S|             |   (if payload len==126/127)   |
            | |1|2|3|       |K|             |                               |
            +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
            |     Extended payload length continued, if payload len == 127  |
            + - - - - - - - - - - - - - - - +-------------------------------+
            |                               |Masking-key, if MASK set to 1  |
            +-------------------------------+-------------------------------+
            | Masking-key (continued)       |          Payload Data         |
            +-------------------------------- - - - - - - - - - - - - - - - +
            :                     Payload Data continued ...                :
            + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
            |                     Payload Data continued ...                |
            +---------------------------------------------------------------+

        ------------------------------------------------------------------------------*/
        struct Frame {
            struct Header {
            protected:
                union {
                    uint16_t bits;
                    struct { /* for GCC, we can use this but not portable (example, OSX cannot process this correctly) */
                        uint8_t opcode:4, rsv3:1, rsv2:1, rsv1:1, fin:1;
                        uint8_t payload_len:7, mask:1;
                    } quick_look;
                } data;
            public:
                /* we should do like below. */
                inline bool fin() const { return (data.bits & (1 << 7)); }
                inline bool rsv1() const { return (data.bits & (1 << 6)); }
                inline bool rsv2() const { return (data.bits & (1 << 5)); }
                inline bool rsv3() const { return (data.bits & (1 << 4)); }
                inline int opcode() const { return (data.bits & 0x000F); }
                inline bool mask() const { return (data.bits & (1 << 15)); }
                inline int payload_len() const { return ((data.bits & 0x7F00) >> 8); }
                inline void set_controls(bool f, bool m, uint8_t opc) {
                    data.bits = 0;
                    if (f) { data.bits |= (1 << 7); }
                    if (m) { data.bits |= (1 << 15); }
                    data.bits |= (opc & 0x0F);
                }
                inline void set_payload_len(uint8_t len) {
                    data.bits |= ((len & 0x7F) << 8);
                }
            };
            union {
                Header h;
                struct {
                    uint16_t padd;
                    uint8_t masking_key[4];
                    uint8_t payload_data[0];
                } mask;
                struct {
                    uint16_t padd;
                    uint8_t payload_data[0];
                } nomask;
                struct {
                    uint16_t padd;
                    uint16_t ext_payload_len;
                    uint8_t masking_key[4];
                    uint8_t payload_data[0];
                } mask_0x7E;
                struct {
                    uint16_t padd;
                    uint16_t ext_payload_len;
                    uint8_t payload_data[0];
                } nomask_0x7E;
                struct {
                    uint16_t padd;
                    uint16_t ext_payload_len[4];
                    uint8_t masking_key[4];
                    uint8_t payload_data[0];
                } mask_0x7F;
                struct {
                    uint16_t padd;
                    uint16_t ext_payload_len[4];
                    uint8_t payload_data[0];
                } nomask_0x7F;
            } ext;

            inline uint8_t get_opcode() const { return ext.h.opcode(); }
            inline bool masked() const { return ext.h.mask(); }
        };
        enum State {
            state_init,
            state_client_handshake,
            state_client_handshake_2,
            state_server_handshake,
            state_established,
            state_recv_frame,
            state_recv_mask,
            state_recv_mask_0x7E,
            state_recv_mask_0x7F,
            state_recv,
            state_recv_0x7E,
            state_recv_0x7F,
        };
        enum opcode {
            opcode_continuation_frame, //*  %x0 denotes a continuation frame
            opcode_text_frame, //*  %x1 denotes a text frame
            opcode_binary_frame, //*  %x2 denotes a binary frame
            //*  %x3-7 are reserved for further non-control frames
            reserved_non_control_frame1,
            reserved_non_control_frame2,
            reserved_non_control_frame3,
            reserved_non_control_frame4,
            reserved_non_control_frame5,

            opcode_connection_close, //*  %x8 denotes a connection close
            opcode_ping, //*  %x9 denotes a ping
            opcode_pong, // *  %xA denotes a pong
            //*  %xB-F are reserved for further control frames
            reserved_control_frame1,
            reserved_control_frame2,
            reserved_control_frame3,
            reserved_control_frame4,
        };
        static const uint32_t CONTROL_FRAME_MAX = 125;
        struct ControlFrame {
            char m_buff[CONTROL_FRAME_MAX];
            uint8_t m_len, padd[2];
            ControlFrame() : m_len(0) {}
            void reset() { m_len = 0; }
            int drain(WebSocketFSM &fsm, SessionFactory::Session &s, size_t remain);
        };
    };
    class WebSocketFSM : public WebSocketProtocol {
    public:
        WebSocketFSM() { set_state(state_server_handshake); }
        explicit WebSocketFSM(const std::string &hostname) : m_hostname(hostname) {
            set_state(state_client_handshake);
        }
        explicit WebSocketFSM(HttpFSM &fsm) : m_sm() {
            m_sm.move_from(fsm);
            set_state(state_established);
        }
        inline bool is_client() const { return m_hostname.length() > 0; }
        inline void set_state(State s) { m_state = s; }
        inline State get_state() const { return static_cast<State>(m_state); }
        int handshake(SessionFactory::Session &s, int r, int w);
        int read_frame(SessionFactory::Session &s, char *p, size_t l);
        int write_frame(SessionFactory::Session &s, const char *p, size_t l,
            opcode opc = opcode_binary_frame, bool masked = true, bool fin = true);
        int send_handshake_request(SessionFactory::Session &s, const char *host);
        int send_handshake_response(SessionFactory::Session &s);
    public:
        void init_frame() { m_flen = 0; m_read = 0; m_mask_idx = 0; }
        void init_key() {
            m_key[0] = random::gen32();
            m_key[1] = random::gen32();
            m_key[2] = random::gen32();
            m_key[3] = random::gen32();
        }
        int read_body_and_fd(SessionFactory::Session &s, char *p, size_t l);
        void ConsumeBody(size_t l) { m_sm_body_read += l; }
        static char *mask_payload(char *p, size_t l, uint32_t mask, uint8_t &mask_idx);
        State analyze_frame(size_t &over_read_length);
        uint32_t get_mask();
        size_t frame_size();
        int drain_recv_data(SessionFactory::Session &s, bool &finished);
        char *init_accept_key_from_header(char *accept_key, size_t accept_key_len);
        char *generate_accept_key_from_value(char *accept_key, size_t accept_key_len);
        static char *generate_accept_key(char *accept_key, size_t accept_key_len, const char *sec_key);
        int verify_handshake();
    protected:
        uint8_t m_state{state_init}, m_flen, m_mask_idx, padd;
        size_t m_sm_body_read{0};
        std::string m_hostname;
        union {
            uint32_t m_key[4];
            uint8_t m_key_ptr[16];
        };
        ControlFrame m_ctrl_frame;
        uint64_t m_read;
        union {
            Frame m_frame;
            char m_frame_buff[sizeof(Frame)];
        };
        HttpFSM m_sm;
    };
    template <class SessionBase>
    class WebSocketSessionT : public SessionBase, public WebSocketProtocol {
    public:
        typedef TcpSessionFactory<typename SessionBase::Factory> Factory;
    protected:
        WebSocketFSM fsm_;
    public:
        WebSocketSessionT(Factory &f, Fd fd, const Address &addr, const std::string &hostname) :
            SessionBase(f, fd, addr), fsm_(hostname) {}
        WebSocketSessionT(Factory &f, Fd fd, const Address &addr) :
            SessionBase(f, fd, addr), fsm_() {}
        WebSocketSessionT(Factory &f, Fd fd, const Address &addr, HttpFSM &fsm) :
            SessionBase(f, fd, addr), fsm_(fsm) {}
        ~WebSocketSessionT() override {}

        inline bool is_client() const { return fsm_.is_client(); }
        inline State get_state() const { return fsm_.get_state(); }
        inline int read_frame(char *p, size_t l) { return fsm_.read_frame(*this, p, l); }
        inline int handshake(int r, int w) { return fsm_.handshake(*this, r, w); }
        inline int send_handshake_response() { return fsm_.send_handshake_response(*this); }
        inline int write_frame(const char *p, size_t l,
            opcode opc = opcode_binary_frame, bool masked = true, bool fin = true) {
            return fsm_.write_frame(*this, p, l, opc, masked, fin);
        }
    public:
        int Send(const char *p, size_t sz) override {
            int r;
            bool masked = is_client();
            if ((r = write_frame(p, sz, opcode_binary_frame, masked)) < 0) {
                if (r != QRPC_EAGAIN) {
                    this->Close(QRPC_CLOSE_REASON_SYSCALL, Syscall::Errno(), Syscall::StrError());
                }
            }
            return r;
        }
        qrpc_time_t OnShutdown() override {
            write_frame("", 0, opcode_connection_close, false);
            return 0;
        }
        void OnEvent(Fd fd, const IoProcessor::Event &e) override {
            int r;
            while (get_state() < state_established) {
                if ((r = handshake(Loop::Readable(e), Loop::Writable(e))) < 0) {
                    if (r != QRPC_EAGAIN) {
                        this->Close(QRPC_CLOSE_REASON_SYSCALL, Syscall::Errno(), Syscall::StrError());
                    }
                    return;
                }
            }
            size_t sz = 4096;
            while (true) {
                char buffer[sz];
                if ((r = read_frame(buffer, sz)) < 0) {
                    break;
                }
                if (r == 0 || (r = this->OnRead(buffer, (size_t)r)) < 0) {
                    this->Close(r == 0 ? QRPC_CLOSE_REASON_REMOTE : QRPC_CLOSE_REASON_LOCAL, r);
                    break;
                }
            }
        }
    };
    typedef WebSocketSessionT<TcpClientSession> WebSocketClientSession;
    typedef WebSocketSessionT<TcpListenerSession> WebSocketListenerSession;
    typedef WebSocketListenerSession WebSocketSession;
    class AdhocWebSocketSession : public WebSocketListenerSession {
    public:
        typedef std::function<int (WebSocketSession &, const char *, size_t)> RecvCallback;
        AdhocWebSocketSession(Factory &f, Fd fd, const Address &addr, HttpFSM &fsm, RecvCallback cb) :
            WebSocketListenerSession(f, fd, addr, fsm), cb_(cb) {}
        ~AdhocWebSocketSession() {}
        int OnRead(const char *p, size_t l) override {
            return cb_(*this, p, l);
        }
    protected:
        RecvCallback cb_;
    };


    /******* WebSocketListener *******/
    class WebSocketListener : public TcpListenerOf<WebSocketSession> {
    public:
        // intend to being called from HttpServer::Callback;
        template <class WS>
        static inline WebSocketSession *Upgrade(HttpListenerSession &s) {
            static_assert(std::is_base_of<WebSocketSession, WS>(), "S must be a descendant of WebSocketSession");
            // ws will be created with established state
            auto ws = new WS(type::cast_or_die<WebSocketListenerSession::Factory>(s.tcp_session_factory()),
                s.fd(), s.addr(), s.fsm());
            return SetupUpgrade(ws, s);
        }
        static inline WebSocketSession *Upgrade(HttpListenerSession &s, AdhocWebSocketSession::RecvCallback cb) {
            auto ws = new AdhocWebSocketSession(
                type::cast_or_die<WebSocketListenerSession::Factory>(s.tcp_session_factory()),
                s.fd(), s.addr(), s.fsm(), cb
            );
            return SetupUpgrade(ws, s);
        }
        template <class WS>
        void Open(const std::string &host, int port) = delete;
    protected:
        static inline WebSocketSession *SetupUpgrade(WebSocketSession *ws, HttpListenerSession &s) {
            int r;
            if ((r = ws->send_handshake_response()) < 0) {
                ASSERT(r != QRPC_ESYSCALL);
                delete ws;
                switch (r) {
                case QRPC_EAGAIN:
                case QRPC_ESYSCALL:
                    s.Unavailable("write() fails");
                    break;
                case QRPC_EINVAL:
                    s.BadRequest("header Sec-WebSocket-Key not found");
                    break;
                }
                return nullptr;
            }
            return ws;
        }
    public:
        static inline int send_handshake_request(SessionFactory::Session &s,
            const char *host, const char *key, const char *origin, const char *protocol = nullptr) {
            /*
            * send client handshake
            * ex)
            * GET / HTTP/1.1
            * Host: server.example.com
            * Upgrade: websocket
            * Connection: Upgrade
            * Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
            * Origin: http://example.com
            * Sec-WebSocket-Protocol: chat, superchat
            * Sec-WebSocket-Version: 13
            */
            char buff[1024], proto_header[1024];
            if (protocol != nullptr) {
                str::Vprintf(proto_header, sizeof(proto_header),
                        "Sec-WebSocket-Protocol: %s\r\n", protocol);
            }
            size_t sz = str::Vprintf(buff, sizeof(buff), 
                    "GET / HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Key: %s\r\n"
                    "Origin: %s\r\n"
                    "%s"
                    "Sec-WebSocket-Version: 13\r\n\r\n",
                    host, key, origin, protocol ? proto_header : "");
            TRACE("ws request %s\n", buff);
            return s.Send(buff, sz);
        }
        static inline int send_handshake_response(SessionFactory::Session &s, const char *accept_key) {
            /*
            * send server handshake
            * ex)
            * HTTP/1.1 101 Switching Protocols
            * Upgrade: websocket
            * Connection: Upgrade
            * Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
            */
            char buff[1024];
            size_t sz = str::Vprintf(buff, sizeof(buff), 
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: %s\r\n\r\n",
                    accept_key);
            TRACE("ws response %s\n", buff);
            return s.Send(buff, sz);
        }
    };

    /******* HttpRouter *******/
    class HttpRouter {
    public:
        typedef std::function<TcpListenerSession *(HttpListenerSession&, std::cmatch&)> Handler;
        typedef HttpFSM Request;
        HttpRouter() {}
        HttpRouter &Route(const std::regex &pattern, const Handler &h) {
            route_.push_back(std::make_pair(pattern, h));
            return *this;
        }
        TcpListenerSession *operator () (HttpListenerSession &s) {
            char buff[256];
            const char *path = s.fsm().url(buff, sizeof(buff));
            if (UNLIKELY(path == nullptr)) {
                s.BadRequest("no path specified\n");
                return nullptr; //session finished
            }
            for (auto &it : route_) {
                std::cmatch match;
                if (std::regex_match(path, match, it.first)) {
                    return it.second(s, match);
                }
            }
            s.NotFound("no route matched for %s\n", path);
            return nullptr; //session finished
        }
    protected:
        std::vector<std::pair<std::regex, Handler>> route_;
    };
}
