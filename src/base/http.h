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
        char *m_p;
    public:
        HttpFSM() : m_p(nullptr) {}
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
        const char  *body() const { return m_ctx.bd; }
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
    class HttpSession : public TcpSession {
    public:
        typedef std::function<TcpSession *(HttpSession&)> Callback;
        struct Header {
            const char *key;
            const char *val;
        };
    public:
        HttpSession(TcpSessionFactory &f, Fd fd, const Address &addr) : TcpSession(f, fd, addr) {
            fsm_.reset(1024);
        }
        ~HttpSession() override {}
        const HttpFSM &req() const { return fsm_; }
        const HttpFSM &fsm() const { return fsm_; }
        HttpFSM &fsm() { return fsm_; }
        TcpSessionFactory &tcp_session_factory() { return factory().to<TcpSessionFactory>(); }
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
                return Syscall::Writev(fd_, ptrs, sizes, hsz + 3);
            } else {
                return Syscall::Writev(fd_, ptrs, sizes, hsz + 2);
            }
        }
        virtual Callback &callback() = 0;
        // implements Session
        int OnRead(const char *p, size_t sz) override;
        int Send(const char *p, size_t sz) override {
            DIE("Send does not supported. use HttpSession::Write instead");
            return QRPC_ENOTSUPPORT;
        }
        // use default for OnConnect/OnShutdown
    public: 
        // utilities
        template<class... Args>
        int Error(http_result_code_t rc, const std::string &fmt, const Args... args) {
            char buffer[1024];
            size_t len = snprintf(buffer, sizeof(buffer), fmt.c_str(), args...);
            std::string lenstr = std::to_string(len);
            Header h[] = {
                {.key = "Content-Type", .val = "text/plain"},
                {.key = "Content-Length", .val = lenstr.c_str()},
            };
            return Respond(rc, h, 2, buffer, len);
        }
        template<class... Args>
        int NotFound(const std::string &fmt, const Args... args) {
            return Error(HRC_NOT_FOUND, fmt, args...);
        }
        template<class... Args>
        int BadRequest(const std::string &fmt, const Args... args) {
            return Error(HRC_BAD_REQUEST, fmt, args...);
        }
        template<class... Args>
        int Unavailable(const std::string &fmt, const Args... args) {
            return Error(HRC_SERVICE_UNAVAILABLE, fmt, args...);
        }
        template<class... Args>
        int ServerError(const std::string &fmt, const Args... args) {
            return Error(HRC_SERVER_ERROR, fmt, args...);
        }
    private:
        HttpFSM fsm_;
    };


    /******* HttpSessionFactory *******/
    class HttpClient : public TcpClient {
    public:
        typedef HttpSession::CloseReason CloseReason;
        class Processor {
        public:
            typedef HttpSession::CloseReason CloseReason;
            virtual ~Processor() {}
            virtual TcpSession *HandleResponse(HttpSession &s) = 0;
            virtual int SendRequest(HttpSession &s) = 0;
            virtual void HandleClose(HttpSession &s, const CloseReason &r) = 0;
        };
        class HttpClientSession : public HttpSession {
        public:
            HttpClientSession(
                HttpClient &c, Fd fd, const Address &a, Processor *p
            ) : HttpSession(c, fd, a), processor_(p), cb_(
                [this](HttpSession &s){ return processor_->HandleResponse(s); }
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
        // https://superuser.com/a/1271864 says chrome timeout is 300s
        HttpClient(Loop &l) : TcpClient(l, qrpc_time_sec(300)) {}
        bool Connect(const std::string &host, int port, Processor *p) {
            return TcpSessionFactory::Connect(host, port, [this, p](Fd fd, const Address &addr) {
                return new HttpClientSession(*this, fd, addr, p);
            });
        }
    };
    class AdhocHttpClient : public HttpClient {
    public:
        typedef std::function<int (HttpSession &)> Sender;
        typedef std::function<TcpSession *(HttpSession &)> Receiver;
        typedef std::function<void (HttpSession &, const CloseReason &)> Closer;
        class Processor : public HttpClient::Processor {
        public:
            Processor(Sender &&scb, Receiver &&rcb, Closer &&ccb) :
                scb_(std::move(scb)), rcb_(std::move(rcb)), ccb_(std::move(ccb)) {}
            Processor(Sender &&scb, Receiver &&rcb) : 
                Processor(std::move(scb), std::move(rcb), Closer(Nop())) {}
            TcpSession *HandleResponse(HttpSession &s) override { return rcb_(s); }
            int SendRequest(HttpSession &s) override { return scb_(s); }
            void HandleClose(HttpSession &s, const CloseReason &r) override { ccb_(s, r); }
        public:
            struct Nop {
                void operator()(HttpSession &, const CloseReason &) {}
            };
        private:
            Sender scb_;
            Receiver rcb_;
            Closer ccb_;
        };
    public:
        AdhocHttpClient(Loop &l) : HttpClient(l) {}
        bool Connect(const std::string &host, int port, Sender &&scb, Receiver &&rcb) {
            return HttpClient::Connect(host, port, new Processor(std::move(scb), std::move(rcb)));
        }
    };


    /******* HttpListener *******/
    class HttpListener : public TcpListener {
    public:
        typedef HttpSession::Callback Callback;
        class HttpServerSession : public HttpSession {
        public:
            HttpServerSession(HttpListener &l, Fd fd, const Address &a) : HttpSession(l, fd, a) {}
            HttpListener &listener() { return factory().to<HttpListener>(); }
            Callback &callback() override { return listener().cb(); }
        };
    public:
        HttpListener(Loop &l, Config c = Config::Default()) : TcpListener(l, [this](Fd fd, const Address &a) {
            return new HttpServerSession(*this, fd, a);
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
    typedef HttpSession::Header HttpHeader;


    /******* WebSocketSession *******/
    class WebSocketSession : public TcpSession {
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
        static const uint32_t MAX_ADDR_LEN = 255;
        static const uint32_t CONTROL_FRAME_MAX = 125;
        static const uint32_t READSIZE = 512;
        struct ControlFrame {
            char m_buff[CONTROL_FRAME_MAX];
            uint8_t m_len, padd[2];
            ControlFrame() : m_len(0) {}
            void reset() { m_len = 0; }
            inline int drain(WebSocketSession &c, size_t remain) {
                int r; 
                if ((r = c.read_body_and_fd(c.fd(), m_buff + m_len, remain)) <= 0) {
                    return r;
                }
                m_len += r;
                return m_len;
            }
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
            opcode_text_frame,	//*  %x1 denotes a text frame
            opcode_binary_frame,	//*  %x2 denotes a binary frame
            //*  %x3-7 are reserved for further non-control frames
            reserved_non_control_frame1,
            reserved_non_control_frame2,
            reserved_non_control_frame3,
            reserved_non_control_frame4,
            reserved_non_control_frame5,

            opcode_connection_close,	//*  %x8 denotes a connection close
            opcode_ping,	//*  %x9 denotes a ping
            opcode_pong,	// *  %xA denotes a pong
            //*  %xB-F are reserved for further control frames
            reserved_control_frame1,
            reserved_control_frame2,
            reserved_control_frame3,
            reserved_control_frame4,
        };
    public:
        uint8_t m_state, m_flen, m_mask_idx, padd;
        size_t m_sm_body_read;
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
    public:
        // create client/server session from begining
        WebSocketSession(TcpSessionFactory &f, Fd fd, const Address &addr, const std::string &hostname) : 
            TcpSession(f, fd, addr),
            m_state(state_client_handshake),
            m_sm_body_read(0), m_hostname(hostname), m_ctrl_frame(), m_sm() {}
        WebSocketSession(TcpSessionFactory &f, Fd fd, const Address &addr) : TcpSession(f, fd, addr),
            m_state(state_server_handshake),
            m_sm_body_read(0), m_hostname(""), m_ctrl_frame(), m_sm() {}
        // for upgrading from http session (as server session)
        WebSocketSession(TcpSessionFactory &f, Fd fd, const Address &addr, HttpFSM &fsm) : TcpSession(f, fd, addr),
            m_state(state_established),
            m_sm_body_read(0), m_hostname(""), m_ctrl_frame(), m_sm() {
            m_sm.move_from(fsm);
        }
        ~WebSocketSession() override {}

        inline bool is_client() const { return m_hostname.length() > 0; }
        inline TcpSessionFactory &tcp_session_factory() { return factory().to<TcpSessionFactory>(); }
    public:
        // implements Session
        int Send(const char *p, size_t sz) override {
            int r;
            // https://datatracker.ietf.org/doc/html/rfc6455#section-5.3
            // masking is only applied to client => server frame transmit
            bool masked = is_client();
            if ((r = WebSocketSession::write_frame(fd_, p, sz, opcode_binary_frame, masked)) < 0) {
                if (r != QRPC_EAGAIN) {
                    Close(QRPC_CLOSE_REASON_SYSCALL, Syscall::Errno(), Syscall::StrError());
                }
            }
            return r;
        }
        qrpc_time_t OnShutdown() override {
            WebSocketSession::write_frame(fd(), "", 0, opcode_connection_close, false);
            return 0;
        }
        // implements IoProcessor (override Session's one)
        void OnEvent(Fd fd, const Event &e) override {
            int r;
            // this is invalid after Close is called
            while (get_state() < state_established) {
                if ((r = handshake(fd, Loop::Readable(e), Loop::Writable(e))) < 0) {
                    if (r != QRPC_EAGAIN) {
                        Close(QRPC_CLOSE_REASON_SYSCALL, Syscall::Errno(), Syscall::StrError());
                    }
                    return;
                }
            }
            size_t sz = 4096;
            while (true) {
                char buffer[sz];
                if ((r = read_frame(fd, buffer, sz)) < 0) {
                    if (r == QRPC_EAGAIN) {
                        return;
                    }
                    Close(QRPC_CLOSE_REASON_SYSCALL, r);
                    break;
                }
                if (r == 0 || (r = OnRead(buffer, (size_t)r)) < 0) {
                    Close(r == 0 ? QRPC_CLOSE_REASON_REMOTE : QRPC_CLOSE_REASON_LOCAL, r);
                    break;
                }
            }
        }
    public:
        inline void init_frame() { m_flen = 0; m_read = 0; m_mask_idx = 0; }
        inline void init_key() {
            m_key[0] = random::gen32();
            m_key[1] = random::gen32();
            m_key[2] = random::gen32();
            m_key[3] = random::gen32();
        }
        // sometimes, first a few frame of websocket received with handshake request.
        // in this timing, receiver is still HTTP mode and store such frame data into
        // body buffer of m_sm. so, we need to consume such data before handling data in socket.
        inline int read_body_and_fd(Fd fd, char *p, size_t l) {
            size_t bl = m_sm.bodylen() - m_sm_body_read;
            size_t copied = 0;
            if (bl > 0) {
                copied += (bl < l ? bl : l);
                Syscall::MemCopy(p, m_sm.body() + m_sm_body_read, copied);
                ConsumeBody(copied);
                l -= copied;
                p += copied;
                if (l == 0) {
                    return copied;
                }
            }
            copied += Syscall::Read(fd, p, l);
            return copied;
        }
        inline void ConsumeBody(size_t l) { m_sm_body_read += l; }
        static inline char *mask_payload(char *p, size_t l, uint32_t mask, uint8_t &mask_idx) {
            char *endp = (p + l);
            if (mask_idx > 0) {
                while (endp > p && mask_idx < sizeof(mask)) {
                    *p = ((*p) ^ (reinterpret_cast<uint8_t *>(&mask))[mask_idx]);
                    p++; mask_idx++;
                }
                if (mask_idx >= sizeof(mask)) {
                    mask_idx = 0;
                }
            }
            while ((endp - p) >= (int)sizeof(uint32_t)) {
                SET_32(p, (GET_32(p) ^ mask));
                p += sizeof(mask);
            }
            size_t remain = (endp - p);
            if (remain > 0) {
                for (; p < endp; p++) {
                    mask_idx = (remain - (endp - p));
                    *p = ((*p) ^ (reinterpret_cast<uint8_t *>(&mask))[mask_idx]);
                }
                mask_idx++;
            }
            return (endp - l);
        }
        inline State analyze_frame(size_t &over_read_length) {
            if (m_flen < sizeof(uint16_t)) {
                return state_recv_frame;
            }
            if (m_frame.ext.h.mask()) {
                if (m_frame.ext.h.payload_len() == 0x7F) {
                    if (m_flen < (sizeof(m_frame.ext.mask_0x7F))) {
                        return state_recv_frame;
                    }
                    over_read_length = (m_flen - (sizeof(m_frame.ext.mask_0x7F)));
                    return state_recv_mask_0x7F;
                }
                else if (m_frame.ext.h.payload_len() == 0x7E) {
                    if (m_flen < (sizeof(m_frame.ext.mask_0x7E))) {
                        return state_recv_frame;
                    }
                    over_read_length = (m_flen - (sizeof(m_frame.ext.mask_0x7E)));
                    return state_recv_mask_0x7E;
                }
                else {
                    if (m_flen < (sizeof(m_frame.ext.mask))) {
                        return state_recv_frame;
                    }
                    over_read_length = (m_flen - (sizeof(m_frame.ext.mask)));
                    return state_recv_mask;
                }
            }
            else {
                if (m_frame.ext.h.payload_len() == 0x7F) {
                    if (m_flen < (sizeof(m_frame.ext.nomask_0x7F))) {
                        return state_recv_frame;
                    }
                    over_read_length = (m_flen - (sizeof(m_frame.ext.nomask_0x7F)));
                    return state_recv_0x7F;
                }
                else if (m_frame.ext.h.payload_len() == 0x7E) {
                    if (m_flen < (sizeof(m_frame.ext.nomask_0x7E))) {
                        return state_recv_frame;
                    }
                    over_read_length = (m_flen - (sizeof(m_frame.ext.nomask_0x7E)));
                    return state_recv_0x7E;
                }
                else {
                    if (m_flen < (sizeof(m_frame.ext.nomask))) {
                        return state_recv_frame;
                    }
                    over_read_length = (m_flen - (sizeof(m_frame.ext.nomask)));
                    return state_recv;
                }
            }
        }
        inline uint32_t get_mask() {
            switch(get_state()) {
            case state_recv_mask:
                return GET_32(m_frame.ext.mask.masking_key);
            case state_recv_mask_0x7E:
                return GET_32(m_frame.ext.mask_0x7E.masking_key);
            case state_recv_mask_0x7F:
                return GET_32(m_frame.ext.mask_0x7F.masking_key);
            default:
                ASSERT(false);
                return 0;
            }
        }
        inline size_t frame_size() {
            switch(get_state()) {
            case state_recv_mask:
                return m_frame.ext.h.payload_len();
            case state_recv_mask_0x7E:
                return ntohs(m_frame.ext.mask_0x7E.ext_payload_len);
            case state_recv_mask_0x7F:
                return ntohll(GET_64(m_frame.ext.mask_0x7F.ext_payload_len));
            case state_recv:
                return m_frame.ext.h.payload_len();
            case state_recv_0x7E:
                return ntohs(m_frame.ext.nomask_0x7E.ext_payload_len);
            case state_recv_0x7F:
                return ntohll(GET_64(m_frame.ext.nomask_0x7F.ext_payload_len));
            default:
                ASSERT(false);
                return 0;
            }
        }
        inline int drain_recv_data(Fd fd, bool &finished) {
            int r; size_t remain = frame_size() - m_read, n_read;
            analyze_frame(n_read);
            if (n_read > 0) {
                Syscall::MemCopy(m_ctrl_frame.m_buff,
                    m_frame_buff + (m_flen - n_read), n_read);
                m_ctrl_frame.m_len += n_read;
            }
            while (remain > 0) {
                if ((r = m_ctrl_frame.drain(*this, remain)) <= 0) {
                    return r;
                }
                m_read += r;
                remain -= r;
            }
            finished = (remain <= 0);
            return QRPC_OK;
        }
        inline int read_frame(Fd fd, char *p, size_t l) {
            int r; size_t remain, n_read;
            char *orgp = p;
        retry:
            TRACE("length = %u %u\n", (int)l, get_state());
            switch(get_state()) {
            case state_established:
                init_frame(); /* fall through */
            case state_recv_frame: {
                if ((r = read_body_and_fd(fd, m_frame_buff + m_flen, sizeof(Frame) - m_flen)) <= 0) {
                    TRACE("read_frame read_body_and_fd fail %d %d\n", r, Syscall::Errno());
                    if (r == 0) { return r; }
                    if (Syscall::EAgain()) {
                        goto again;
                    }
                    goto error;
                }
                m_flen += r;
                m_state = analyze_frame(n_read);
                if (m_state <= state_recv_frame) {
                    goto again;
                }
                if (n_read > 0) {
                    if (l < n_read) {
                        return QRPC_ESIZE;
                    }
                    Syscall::MemCopy(p, m_frame_buff + (m_flen - n_read), n_read);
                    if (m_frame.masked()) {
                        mask_payload(p, n_read, get_mask(), m_mask_idx);
                    }
                    p += n_read;
                    l -= n_read;
                    m_read += n_read;
                    TRACE("read %u byte\n", (int)n_read);
                }
            }  /* fall through */
            case state_recv_mask:
            case state_recv_mask_0x7E:
            case state_recv_mask_0x7F:
            case state_recv:
            case state_recv_0x7E:
            case state_recv_0x7F: {
                TRACE("opcode = %u, flen=%u\n", m_frame.get_opcode(), (int)frame_size());
                switch(m_frame.get_opcode()) {
                case opcode_continuation_frame:
                case opcode_text_frame:
                case opcode_binary_frame: {
                    remain = frame_size() - m_read;
                    if (remain <= 0) {
                        if (m_read <= 0) {
                            TRACE("non-control frame has no data\n");
                            ASSERT(false);
                            goto error;
                        }
                        // read all of current frame. new frame will be read next
                        m_state = state_established;
                        goto retry;
                    }
                    n_read = l;
                    if (n_read > remain) { n_read = remain; }
                    if ((r = read_body_and_fd(fd, p, n_read)) <= 0) {
                        if (r == 0) { return r; }
                        if (Syscall::EAgain()) {
                            goto again;
                        }
                        goto error;
                    }
                    if (m_frame.masked()) {
                        mask_payload(p, r, get_mask(), m_mask_idx);
                    }
                    m_read += r;
                    p += r;
                    l -= r;
                    TRACE("read %u byte\n", r);
                } break;
                case opcode_connection_close: {
                    /* body has 2 byte to indicate why connection close */
                    bool finished;
                    if ((r = drain_recv_data(fd, finished)) <= 0) {
                        if (r == 0) { return r; }
                        if (Syscall::EAgain()) {
                            if (m_frame.masked()) {
                                mask_payload(m_ctrl_frame.m_buff, m_ctrl_frame.m_len, get_mask(), m_mask_idx);
                            }
                            goto again;
                        }
                        goto error;
                    }
                    if (finished) {
                        if (m_frame.masked()) {
                            mask_payload(m_ctrl_frame.m_buff, m_ctrl_frame.m_len, get_mask(), m_mask_idx);
                        }
                        TRACE("close reason : %u\n", GET_16(m_ctrl_frame.m_buff));
                        m_ctrl_frame.reset();
                        Close(QRPC_CLOSE_REASON_REMOTE, GET_16(m_ctrl_frame.m_buff), "websocket close frame received");
                    }
                } break;
                case opcode_ping:
                case opcode_pong: {
                    bool finished;
                    if ((r = drain_recv_data(fd, finished)) <= 0) {
                        if (r == 0) { return r; }
                        if (Syscall::EAgain()) {
                            goto again;
                        }
                        goto error;
                    }
                    if (finished) {
                        if (m_frame.get_opcode() == opcode_ping) {
                            if (m_frame.masked()) {
                                mask_payload(m_ctrl_frame.m_buff, m_ctrl_frame.m_len,
                                    get_mask(), m_mask_idx);
                            }
                            /* return pong */
                            WebSocketSession::write_frame(fd,
                                m_ctrl_frame.m_buff,
                                m_ctrl_frame.m_len,
                                opcode_pong,
                                m_frame.masked()
                            );
                            /* even if pong fails, keep on. */
                        }
                        m_ctrl_frame.reset();
                    }
                } break;
                }
                if (l > 0) {
                    TRACE("%u byte remains. retry\n", (int)l);
                    goto retry;
                }
                return p - orgp;
            } break;
            default:
                ASSERT(false);
                return QRPC_EINVAL;
            }
        again:
            if (orgp < p) {
                return p - orgp;
            }
            return QRPC_EAGAIN;
        error:
            return QRPC_EINVAL;
        }
        /* no fragmentation support (TODO) */
        static inline int write_frame(Fd fd, const char *p, size_t l,
            opcode opc = opcode_binary_frame, bool masked = true, bool fin = true) {
            char buff[sizeof(Frame)]; uint32_t rnd; uint8_t idx = 0;
            Frame *pf = reinterpret_cast<Frame *>(buff);
            size_t hl; Frame frm;
            pf->ext.h.set_controls(fin, masked, opc);
            ASSERT(fin == pf->ext.h.fin());
            if (l >= 0x7E) {
                if (l <= 0xFFFF) {
                    pf->ext.h.set_payload_len(0x7E);
                    if (pf->ext.h.mask()) {
                        rnd = random::gen32();
                        pf->ext.mask_0x7E.ext_payload_len = htons(l);
                        SET_32(pf->ext.mask_0x7E.masking_key, rnd);
                        hl = sizeof(frm.ext.mask_0x7E);
                    }
                    else {
                        pf->ext.nomask_0x7E.ext_payload_len =  htons(l);
                        hl = sizeof(frm.ext.nomask_0x7E);
                    }
                }
                else {
                    pf->ext.h.set_payload_len(0x7F);
                    if (pf->ext.h.mask()) {
                        rnd = random::gen32();
                        SET_64(pf->ext.mask_0x7F.ext_payload_len, htonll(l));
                        SET_32(pf->ext.mask_0x7F.masking_key, rnd);
                        hl = sizeof(frm.ext.mask_0x7F);
                    }
                    else {
                        SET_64(pf->ext.nomask_0x7F.ext_payload_len, htonll(l));
                        hl = sizeof(frm.ext.nomask_0x7F);
                    }
                }
            }
            else {
                pf->ext.h.set_payload_len(l);
                if (pf->ext.h.mask()) {
                    rnd = random::gen32();
                    SET_32(pf->ext.mask.masking_key, rnd);
                    hl = sizeof(frm.ext.mask);
                }
                else {
                    hl = sizeof(frm.ext.nomask);
                }
            }
            if (Syscall::Write(fd, buff, hl) < 0) {
                return QRPC_ESYSCALL;
            }
            int r = (masked ?
                Syscall::Write(fd, mask_payload(const_cast<char *>(p), l, rnd, idx), l) :
                Syscall::Write(fd, p, l));
            /* cannot send all packet */
            if (r < 0 || ((size_t)r) < l) {
                ASSERT(Syscall::Errno() == EPIPE);
                return QRPC_ESYSCALL;
            }
            return r;
        }
        inline char *init_accept_key_from_header(char *accept_key, size_t accept_key_len) {
            /* get key from websocket header */
            char kbuf[256]; int kblen;
            if (!m_sm.hdrstr("Sec-Websocket-Key", kbuf, sizeof(kbuf), &kblen)) {
                return nullptr;
            }
            uint8_t vbuf[256];	//it should be 16 byte
            if (sizeof(m_key_ptr) != base64::decode(kbuf, kblen, vbuf, sizeof(vbuf))) {
                return nullptr;
            }
            Syscall::MemCopy(m_key_ptr, vbuf, sizeof(m_key_ptr));
            return generate_accept_key(accept_key, accept_key_len, kbuf);
        }
        inline char *generate_accept_key_from_value(char *accept_key, size_t accept_key_len) {
            // https://datatracker.ietf.org/doc/html/rfc6455#section-4.2.2 4 /key/
            // The |Sec-WebSocket-Key| header field in the client's handshake
            // includes a base64-encoded value that, if decoded, is 16 bytes in length
            STATIC_ASSERT(sizeof(m_key_ptr) == 16);
            /* base64 encode */
            char enc[base64::buffsize(sizeof(m_key_ptr))];
            base64::encode(m_key_ptr, sizeof(m_key_ptr), enc, sizeof(enc));
            return generate_accept_key(accept_key, accept_key_len, enc);
        }
        static inline char *generate_accept_key(char *accept_key, size_t accept_key_len, const char *sec_key) {
            if (accept_key_len < base64::buffsize(sha1::kDigestSize)) {
                ASSERT(false); return nullptr;
            }
            /* add salt */
            char work[256];
            /* this value is decided by RFC */
            char salt[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            size_t l = str::Vprintf(work, sizeof(work), "%s%s", sec_key, salt);
            /* encoded by SHA-1(160bit), digest is internally managed and no need to free */
            const uint8_t *digest = sha1::digest(work, l);
            /* base64 encode */
            base64::encode(digest, sha1::kDigestSize, accept_key, sizeof(accept_key));
            return accept_key;
        }
        int send_handshake_request(const char *host);
        int send_handshake_response();
        #define HS_CHECK(cond, ...)	if (!(cond)) { TRACE(__VA_ARGS__); return QRPC_EINVAL; }
        inline int verify_handshake() {
            char tok[256];
            HS_CHECK(m_sm.hdrstr("Upgrade", tok, sizeof(tok)), "Upgrade header\n");
            HS_CHECK(str::CmpNocase(tok, "websocket", sizeof(tok)) == 0,
                "Upgrade invalid %s\n", tok);
            HS_CHECK(m_sm.hdrstr("Connection", tok, sizeof(tok)), "Connection header\n");
            HS_CHECK(str::CmpNocase(tok, "upgrade", sizeof(tok)) == 0,
                "Connection invalid %s\n", tok);
            switch(get_state()) {
            case state_client_handshake_2: {
                char calculated[base64::buffsize(sha1::kDigestSize)];
                HS_CHECK(m_sm.rc() == HRC_SWITCHING_PROTOCOLS, "invalid response %d\n", m_sm.rc());
                HS_CHECK(m_sm.hdrstr("Sec-WebSocket-Accept", tok, sizeof(tok)) != nullptr,
                    "Sec-WebSocket-Accept header\n");
                HS_CHECK(nullptr != generate_accept_key_from_value(calculated, sizeof(calculated)),
                    "cannot calculate accept key from client data\n");
                HS_CHECK(str::CmpNocase(tok, calculated, sizeof(calculated)) == 0,
                    "Sec-WebSocket-Accept Invalid: [%s], should be [%s]\n", tok, calculated);
            } return QRPC_OK;
            case state_server_handshake: {
                HS_CHECK(m_sm.hashdr("Host"), "Host header\n");
                HS_CHECK(m_sm.hashdr("Sec-WebSocket-Key"), "Sec-WebSocket-Key header\n");
                /* TODO: optional header check? */
                int v;
                HS_CHECK(m_sm.hdrint("Sec-WebSocket-version", v) >= 0, "Sec-WebSocket-version header\n");
                HS_CHECK(v == 13, "version invalid %u\n", v);
            } return QRPC_OK;
            default:
                ASSERT(false);
                return QRPC_EINVAL;
            }
        }
        int handshake(Fd fd, int r, int w) {
            char rbf[4096]; int rsz;
            TRACE("WebSocketSession::handshake: %d %d %d %d\n", fd, get_state(), r, w);
            switch(get_state()) {
            case state_client_handshake: {
                if (!w) { return QRPC_EAGAIN; }
                if (send_handshake_request(m_hostname.c_str()) < 0) {
                    return Syscall::EAgain() ? QRPC_EAGAIN : QRPC_ESYSCALL;
                }
                set_state(state_client_handshake_2);
                return QRPC_EAGAIN;	//next state require read first
            }
            case state_client_handshake_2:
            case state_server_handshake: {
                if (!r) { return QRPC_EAGAIN; }
                if ((rsz = Syscall::Read(fd, rbf, sizeof(rbf))) < 0) { 
                    return Syscall::EAgain() ? QRPC_EAGAIN : QRPC_ESYSCALL;
                }
                TRACE("receive handshake packet [%s](%u)\n", rbf, rsz);
                HttpFSM::state s = m_sm.append(rbf, rsz);
                if (s == HttpFSM::state_recv_header) { return QRPC_EAGAIN; }
                else if (s == HttpFSM::state_websocket_establish) {
                    int err;
                    if ((err = verify_handshake()) < 0) {
                        ASSERT(false);
                        return err;
                    }
                    if (get_state() == state_server_handshake) {
                        if (send_handshake_response() < 0) {
                            return Syscall::EAgain() ? QRPC_EAGAIN : QRPC_ESYSCALL;
                        }
                    }
                    set_state(state_established);
                    return QRPC_OK;
                }
                ASSERT(false);
                return QRPC_EINVAL;
            }
            default:
                ASSERT(false);
                return QRPC_EINVAL;
            }
        }
        inline void set_state(State s) { m_state = s; }
        inline State get_state() const { return static_cast<State>(m_state); }
    };
    class AdhocWebSocketSession : public WebSocketSession {
    public:
        typedef std::function<int (WebSocketSession &, const char *, size_t)> RecvCallback;
        AdhocWebSocketSession(TcpSessionFactory &f, Fd fd, const Address &addr, HttpFSM &fsm, RecvCallback cb) :
            WebSocketSession(f, fd, addr, fsm), cb_(cb) {}
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
        static inline WebSocketSession *Upgrade(HttpSession &s) {
            static_assert(std::is_base_of<WebSocketSession, WS>(), "S must be a descendant of WebSocketSession");
            // ws will be created with established state
            auto ws = new WS(s.tcp_session_factory(), s.fd(), s.addr(), s.fsm());
            return SetupUpgrade(ws, s);
        }
        static inline WebSocketSession *Upgrade(HttpSession &s, AdhocWebSocketSession::RecvCallback cb) {
            auto ws = new AdhocWebSocketSession(s.tcp_session_factory(), s.fd(), s.addr(), s.fsm(), cb);
            return SetupUpgrade(ws, s);
        }
        template <class WS>
        void Open(const std::string &host, int port) {
            static_assert(std::is_base_of<WebSocketSession, WS>(), "S must be a descendant of WebSocketSession");
            TcpListenerOf<WebSocketSession>::Connect(host, port, [this, host](Fd fd, const Address &addr) {
                return new WS(*this, fd, addr, host);
            }, AF_INET);
        }
    protected:
        static inline WebSocketSession *SetupUpgrade(WebSocketSession *ws, HttpSession &s) {
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
        static inline int send_handshake_request(Fd fd,
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
            return Syscall::Write(fd, buff, sz);
        }
        static inline int send_handshake_response(Fd fd, const char *accept_key) {
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
            return Syscall::Write(fd, buff, sz);
        }
    };

    /******* HttpRouter *******/
    class HttpRouter {
    public:
        typedef std::function<TcpSession *(HttpSession&, std::cmatch&)> Handler;
        typedef HttpFSM Request;
        HttpRouter() : route_() {}
        HttpRouter &Route(const std::regex &pattern, const Handler &h) {
            route_.push_back(std::make_pair(pattern, h));
            return *this;
        }
        TcpSession *operator () (HttpSession &s) {
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
