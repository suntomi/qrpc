//this file is shared... so please not include client specific headers (eg. for TRACE)
#include "base/http.h"
#include <memory.h>
#include <stdlib.h>

#include <thread>

#include "base/logger.h"

#define EXPAND_BUFFER

namespace base {
    /******* HttpFSM functions *******/
    void
    HttpFSM::reset(uint32_t chunk_size)
    {
        m_buf = m_p = (char *)malloc(chunk_size);
        ASSERT(m_p != nullptr);
        m_len = 0;
        m_max = chunk_size;
        m_ctx.version = version_1_1;
        m_ctx.n_hd = 0;
        m_ctx.bd = nullptr;
        m_ctx.state = state_recv_header;
    }

    HttpFSM::state
    HttpFSM::append(const char *b, int bl)
    {
        //  TRACE("append %u byte <%s>\n", bl, b);
        state s = get_state();
        const char *w = b;
        uint32_t limit = (m_max - 1);
        while (s != state_error && s != state_recv_finish) {
            if (m_len >= limit) {
    #if defined(EXPAND_BUFFER)
                //try expand buffer
                char *org = m_p;
                m_p = (char *)realloc(m_p, limit * 2);
                m_max = limit * 2;
                m_buf = m_p + (m_buf - org);
                for (int i = 0; i < m_ctx.n_hd; i++) {
                    m_ctx.hd[i] = m_p + (m_ctx.hd[i] - org);
                }
                if (m_ctx.bd != nullptr) {
                    m_ctx.bd = m_p + (m_ctx.bd - org);
                }
    #else
                s = state_error;
                break;
    #endif
            }
            m_p[m_len++] = *w++;
            m_p[m_len] = '\0';
    #if defined(_DEBUG)
            //      if ((m_len % 100) == 0) { TRACE("."); }
            //      TRACE("recv[%u]:%u\n", m_len, s);
    #endif
            switch(s) {
                case state_recv_header:
                    s = recv_header(); break;
                case state_recv_body:
                    s = recv_body(); break;
                case state_recv_body_nochunk:
                    s = recv_body_nochunk(); break;
                case state_recv_bodylen:
                    s = recv_bodylen(); break;
                case state_recv_footer:
                    s = recv_footer(); break;
                case state_recv_comment:
                    s = recv_comment(); break;
                case state_websocket_establish:
                    goto end;
                default:
                    break;
            }
            if ((w - b) >= bl) { break; }
        }
    end:
        recvctx().state = (uint16_t)s;
        return s;
    }

    char*
    HttpFSM::hdrstr(const char *key, char *b, int l, int *outlen) const
    {
        for (int i = 0; i < m_ctx.n_hd; i++) {
            const char *k = key;
            const char *p = m_ctx.hd[i];
            /* key name comparison by case non-sensitive */
            while (*k && tolower(*k) == tolower(*p)) {
                if ((k - key) > m_ctx.hl[i]) {
                    ASSERT(false);
                    return NULL;    /* key name too long */
                }
                k++; p++;
            }
            if (*k) {
                continue;   /* key name and header tag not match */
            }
            else {
                /* seems header is found */
                while (*p) {
                    /* skip [spaces][:][spaces] between [tag] and [val] */
                    if (*p == ' ' || *p == ':') { p++; }
                    else { break; }
                    if ((m_ctx.hd[i] - p) > m_ctx.hl[i]) {
                        ASSERT(false);
                        return NULL;    /* too long space(' ') */
                    }
                }
                char *w = b;
                while (*p) {
                    *w++ = *p++;
                    if ((w - b) >= l) {
                        ASSERT(false);
                        return NULL;    /* too long header paramter */
                    }
                }
                if (outlen) {
                    *outlen = (int)(w - b);
                }
                *w = 0; /* null terminate */
                return b;
            }
        }
        return NULL;
    }

    bool
    HttpFSM::hdrint(const char *key, int &out) const
    {
        char b[256];
        if (NULL != hdrstr(key, b, sizeof(b))) {
            try {
                size_t idx;
                out = std::stoi(b, &idx);
                if (b[idx] != 0) {
                    return false;
                }
            } catch (std::exception &e) {
                return false;
            }
            return true;
        }
        return false;
    }

    int
    HttpFSM::recv_lf() const
    {
        const char *p = current();
        //  if (m_len > 1) {
        //      TRACE("now last 2byte=<%s:%u>%u\n", (p - 2), GET_16(p - 2), htons(crlf));
        //  }
        if (m_len > 2 && GET_16(p - 2) == htons(crlf)) {
            return 2;
        }
        if (m_len > 1 && *(p - 1) == '\n') {
            return 1;
        }
        return 0;
    }

    int
    HttpFSM::recv_lflf() const
    {
        const char *p = current();
        if (m_len > 4 && GET_32(p - 4) == htonl(crlfcrlf)) {
            return 4;
        }
        if (m_len > 2 && GET_16(p - 2) == htons(lflf)) {
            return 2;
        }
        return 0;
    }

    HttpFSM::state
    HttpFSM::recv_header()
    {
        char *p = current();
        int nlf, tmp;
        if ((nlf = recv_lf())) {
            /* lf found but line is empty. means \n\n or \r\n\r\n */
            tmp = nlf;
            for (;tmp > 0; tmp--) {
                *(p - tmp) = '\0';
            }
            if ((p - nlf) == m_buf) {
                int cl; char tok[256];
                /* get result code */
                m_ctx.res = putrc();
                /* if content length is exist, no chunk encoding */
                if (hdrint("Content-Length", cl)) {
                    recvctx().bd = p;
                    recvctx().bl = cl;
                    return state_recv_body_nochunk;
                }
                /* if chunk encoding, process as chunk */
                else if (hdrstr("Transfer-Encoding", tok, sizeof(tok)) != NULL &&
                         memcmp(tok, "chunked", sizeof("chunked") - 1) == 0) {
                    m_buf = recvctx().bd = p;
                    recvctx().bl = 0;
                    return state_recv_bodylen;
                }
                // server or client websocket handshake
                else if (hdrstr("Sec-WebSocket-Key", tok, sizeof(tok)) ||
                         hdrstr("Sec-WebSocket-Accept", tok, sizeof(tok))) {
                    return state_websocket_establish;
                }
                else if (rc() == HRC_OK){
                    return state_error;
                }
                else { return state_recv_finish; }
            }
            /* lf found. */
            else if (recvctx().n_hd < MAX_HEADER) {
                recvctx().hd[recvctx().n_hd] = m_buf;
                recvctx().hl[recvctx().n_hd] = (p - m_buf) - nlf;
                m_buf = p;
                recvctx().n_hd++;
            }
            else {  /* too much header. */
                return state_error;
            }
        }
        return state_recv_header;
    }

    HttpFSM::state
    HttpFSM::recv_body()
    {
        int nlf;
        if ((nlf = recv_lf())) {
            /* some stupid web server contains \n in its response...
             * so we check actual length is received */
            long n_diff = (recvctx().bd + recvctx().bl) - (m_p + m_len - nlf);
            if (n_diff > 0) {
                /* maybe \r\n will come next */
                return state_recv_body;
            }
            else if (n_diff < 0) {
                /* it should not happen even if \n is contained */
                return state_error;
            }
            m_len -= nlf;
            m_buf = current();
            return state_recv_bodylen;
        }
        return state_recv_body;
    }

    HttpFSM::state
    HttpFSM::recv_body_nochunk()
    {
        long diff = (recvctx().bd + recvctx().bl) - (m_p + m_len);
        ASSERT(recvctx().bd[0] != 'P');
        if (diff > 0) {
            return state_recv_body_nochunk;
        }
        else if (diff < 0) {
            return state_error;
        }
        return state_recv_finish;
    }

    HttpFSM::state
    HttpFSM::recv_bodylen()
    {
        char *p = current();
        state s = state_recv_bodylen;
        
        int nlf;
        if ((nlf = recv_lf())) {
            s = state_recv_body;
        }
        else if (*p == ';') {
            /* comment is specified after length */
            nlf = 1;
            s = state_recv_comment;
        }
        if (s != state_recv_bodylen) {
            int cl;
            for (;nlf > 0; nlf--) {
                *(p - nlf) = '\0';
            }
            if (!htoi(m_buf, &cl, (p - m_buf))) {
                return state_error;
            }
            /* 0-length chunk means chunk end -> next footer */
            if (cl == 0) {
                m_buf = p;
                return state_recv_footer;
            }
            recvctx().bl += cl;
            m_len -= (p - m_buf);
        }
        return s;
    }

    HttpFSM::state
    HttpFSM::recv_footer()
    {
        char *p = current();
        int nlf, tmp;
        if ((nlf = recv_lf())) {
            tmp = nlf;
            for (;tmp > 0; tmp--) {
                *(p - tmp) = '\0';
            }
            /* lf found but line is empty. means \n\n or \r\n\r\n */
            if ((p - nlf) == m_buf) {
                return state_recv_finish;
            }
            /* lf found. */
            else if (recvctx().n_hd < MAX_HEADER) {
                recvctx().hd[recvctx().n_hd] = m_buf;
                recvctx().hl[recvctx().n_hd] = (p - m_buf) - nlf;
                *p = '\0';
                m_buf = p;
                recvctx().n_hd++;
            }
            else {  /* too much footer + header. */
                return state_error;
            }
        }
        return state_recv_footer;
    }

    HttpFSM::state
    HttpFSM::recv_comment()
    {
        int nlf;
        if ((nlf = recv_lf())) {
            char *p = current();
            m_len -= (p - m_buf);
            return state_recv_body;
        }
        return state_recv_comment;
    }

    const char *
    HttpFSM::url(char *b, int l, size_t *p_out)
    {
        const char *w = m_ctx.hd[0];
        /* skip first verb (GET/POST/etc...) */
        while (!std::isspace(*w)) {
            w++;
            if ((w - m_ctx.hd[0]) > m_ctx.hl[0]) {
                return nullptr;
            }
            /* reach to end of string: format error */
            if (*w == '\0') { return nullptr; }
        }
        // skip spaces between verb and path
        while (std::isspace(*w)) {
            w++;
            if ((w - m_ctx.hd[0]) > m_ctx.hl[0]) {
                return nullptr;
            }
        }
        char *wb = b;
        while (!std::isspace(*w)) {
            *wb++ = *w++;
            if ((wb - b) > l) {
                return nullptr;
            }
            if (*w == '\0') { return nullptr; }
        }
        *wb = '\0';
        if (p_out != nullptr) {
            *p_out = (wb - b);
        }
        return b;
    }

    bool
    HttpFSM::htoi(const char* str, int *i, size_t max)
    {
        const char *_s = str;
        int minus = 0;
        *i = 0;
        if ('-' == *_s) {
            minus = 1;
            _s++;
        }
        while(*_s) {
            int num = -1;
            if ('0' <= *_s && *_s <= '9') {
                num = (int)((*_s) - '0');
            }
            if ('a' <= *_s && *_s <= 'f') {
                num = (int)(((*_s) - 'a') + 10);
            }
            if ('A' <= *_s && *_s <= 'F') {
                num = (int)(((*_s) - 'A') + 10);
            }
            if (num < 0) {
                return false;
            }
            (*i) = (*i) * 16 + num;
            _s++;
            // _s is increment of str, so always > 0
            if ((size_t)(_s - str) >= max) {
                return false;
            }
        }
        
        if (minus) {
            (*i) = -1 * (*i);
        }
        
        return true;
    }

    bool
    HttpFSM::atoi(const char* str, int *i, size_t max)
    {
        const char *_s = str;
        int minus = 0;
        *i = 0;
        if ('-' == *_s) {
            minus = 1;
            _s++;
        }
        while(*_s) {
            if ('0' <= *_s && *_s <= '9') {
                (*i) = (*i) * 10 + (int)((*_s) - '0');
            }
            else {
                return false;
            }
            _s++;
            // _s is increment of str, so always > 0
            if ((size_t)(_s - str) >= max) {
                return false;
            }
        }
        
        if (minus) {
            (*i) = -1 * (*i);
        }
        
        return true;
    }

    HttpFSM::result_code
    HttpFSM::putrc()
    {
        const char *w = m_ctx.hd[0], *s = w;
        w += 5; /* skip first 5 character (HTTP/) */
        if (memcmp(w, "1.1", sizeof("1.1") - 1) == 0) {
            m_ctx.version = 11;
            w += 3;
        }
        else if (memcmp(w, "1.0", sizeof("1.0") - 1) == 0) {
            m_ctx.version = 10;
            w += 3;
        }
        else {
            return HRC_ERROR;
        }
        char tok[256];
        char *t = tok;
        while(*w) {
            w++;
            if (*w != ' ') { break; }
            if ((w - s) > m_ctx.hl[0]) {
                return HRC_ERROR;
            }
        }
        while(*w) {
            if (*w == ' ') { break; }
            *t++ = *w++;
            if ((w - s) > m_ctx.hl[0]) {
                return HRC_ERROR;
            }
            if ((unsigned int )(t - tok) >= sizeof(tok)) {
                return HRC_ERROR;
            }
        }
        int sc;
        *t = '\0';
        if (!atoi(tok, &sc, sizeof(tok))) {
            return HRC_ERROR;
        }
        return (result_code)sc;
    }

    bool HttpFSM::hdr_contains(const char *header_name, const char *content) const
    {
        int hdlen;
        char buffer[256];
        const char *value = hdrstr(header_name, buffer, sizeof(buffer), &hdlen);
        if (value != nullptr) {
            if (strstr(buffer, content) != nullptr) {
                return true;
            } else {
                return false;
            }
        }
        //if no header found, regard peer as can accept anything.
        return true;
    }


    /******* HttpSession *******/
    int HttpSession::OnRead(const char *p, size_t sz) {
        fsm_.append(p, sz);
        switch (fsm_.get_state()) {
        case HttpFSM::state_recv_header:
        case HttpFSM::state_recv_body:
        case HttpFSM::state_recv_body_nochunk:
        case HttpFSM::state_recv_bodylen:
        case HttpFSM::state_recv_footer:
        case HttpFSM::state_recv_comment:
            return QRPC_OK; //not close connection
        case HttpFSM::state_websocket_establish:
        case HttpFSM::state_recv_finish: {
            auto newsession = factory_.to<HttpListener>().cb()(*this);
            if (newsession != nullptr) {
                ASSERT(newsession->fd() == fd_);
                if (newsession == this) {
                    fsm_.set_state(HttpFSM::state_response_pending);
                    // session does not closed here (deferred).
                    // callbacked module should cleanup connection after response is sent,
                    // by calling Close(...)
                } else {
                    // fd is migrate to other session. eg WebSocket.
                    // need to delete this
                    MigrateTo(newsession);
                }
            } else {
                Close(QRPC_CLOSE_REASON_LOCAL);
                // after here, cannot touch this object.
            }
            return QRPC_OK;
        } break;
        case HttpFSM::state_invalid:
        case HttpFSM::state_error:
        case HttpFSM::state_response_pending:
        default:
            ASSERT(false);
        }
        return QRPC_EINVAL; // close connection
    }

    int WebSocketSession::send_handshake_request(const char *host) {
        init_key();
        char out[base64::buffsize(sizeof(m_key_ptr))], origin[256];
        base64::encode(m_key_ptr, sizeof(m_key_ptr), out, sizeof(out));
        str::Vprintf(origin, sizeof(origin), "http://%s", host);
        auto r = WebSocketListener::send_handshake_request(fd(), host, out, origin, NULL);
        if (r < 0) { return Syscall::WriteMayBlocked(r, false) ? QRPC_EAGAIN : QRPC_ESYSCALL; }
        return r;
    }
    int WebSocketSession::send_handshake_response() {
        char buffer[base64::buffsize(sha1::kDigestSize)], *p;
        if (!(p = init_accept_key_from_header(buffer, sizeof(buffer)))) {
            return QRPC_EINVAL;
        }
        auto r = WebSocketListener::send_handshake_response(fd(), buffer);
        if (r < 0) { return Syscall::WriteMayBlocked(r, false) ? QRPC_EAGAIN : QRPC_ESYSCALL; }
        return r;
    }
}
