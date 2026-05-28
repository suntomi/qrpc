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
        state s = get_state();
        const char *w = b;
        while (s != state_error && s != state_recv_finish) {
            if (m_len >= m_max) {
    #if defined(EXPAND_BUFFER)
                //try expand buffer
                char *org = m_p;
                m_p = (char *)realloc(m_p, m_max * 2);
                m_max = m_max * 2;
                // m_buf, m_ctx.hd[i], m_ctx.bd is pointer which is offset of old m_p
                // so, calculate offset using old m_p, is completely valid.
                DISABLE_USE_AFTER_FREE_WARNING_PUSH
                m_buf = m_p + (m_buf - org);
                for (int i = 0; i < m_ctx.n_hd; i++) {
                    m_ctx.hd[i] = m_p + (m_ctx.hd[i] - org);
                }
                if (m_ctx.bd != nullptr) {
                    m_ctx.bd = m_p + (m_ctx.bd - org);
                }
                DISABLE_USE_AFTER_FREE_WARNING_POP
    #else
                s = state_error;
                ASSERT(false);
                break;
    #endif
            }
            ASSERT(m_len < m_max);
            m_p[m_len++] = *w++;
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
                    recvctx().bd = nullptr;
                    recvctx().bl = 0;
                    return state_websocket_establish;
                }
                else if (rc() == HRC_OK){
                    return state_error;
                }
                else {
                    recvctx().bd = nullptr;
                    recvctx().bl = 0;
                    return state_recv_finish;
                }
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


    /******* HttpProtocol *******/
    int HttpProtocol::ProcessRead(HttpFSM &fsm, const char *p, size_t sz) {
        fsm.append(p, sz);
        switch (fsm.get_state()) {
        case HttpFSM::state_recv_header:
        case HttpFSM::state_recv_body:
        case HttpFSM::state_recv_body_nochunk:
        case HttpFSM::state_recv_bodylen:
        case HttpFSM::state_recv_footer:
        case HttpFSM::state_recv_comment:
            return QRPC_OK; //not close connection
        case HttpFSM::state_websocket_establish:
        case HttpFSM::state_recv_finish:
            return OnFinishRead();
        case HttpFSM::state_invalid:
        case HttpFSM::state_error:
        case HttpFSM::state_response_pending:
        default:
            ASSERT(false);
        }
        return QRPC_EINVAL; // close connection
    }

    /******* WebSocketFSM *******/
    int WebSocketProtocol::ControlFrame::drain(WebSocketFSM &fsm, SessionFactory::Session &s, size_t remain) {
        int r;
        if ((r = fsm.read_body_and_fd(s, m_buff + m_len, remain)) <= 0) {
            return r;
        }
        m_len += r;
        return m_len;
    }
    int WebSocketFSM::read_body_and_fd(SessionFactory::Session &s, char *p, size_t l) {
        size_t bl = m_sm.bodylen() - m_sm_body_read;
        size_t copied = 0;
        if (bl > 0) {
            copied += (bl < l ? bl : l);
            Syscall::MemCopy(p, m_sm.bodyptr() + m_sm_body_read, copied);
            ConsumeBody(copied);
            l -= copied;
            p += copied;
            if (l == 0) {
                return copied;
            }
        }
        copied += s.Read(p, l);
        return copied;
    }
    char *WebSocketFSM::mask_payload(char *p, size_t l, uint32_t mask, uint8_t &mask_idx) {
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
    WebSocketFSM::State WebSocketFSM::analyze_frame(size_t &over_read_length) {
        if (m_flen < sizeof(uint16_t)) { return state_recv_frame; }
        if (m_frame.ext.h.mask()) {
            if (m_frame.ext.h.payload_len() == 0x7F) {
                if (m_flen < sizeof(m_frame.ext.mask_0x7F)) { return state_recv_frame; }
                over_read_length = (m_flen - sizeof(m_frame.ext.mask_0x7F));
                return state_recv_mask_0x7F;
            } else if (m_frame.ext.h.payload_len() == 0x7E) {
                if (m_flen < sizeof(m_frame.ext.mask_0x7E)) { return state_recv_frame; }
                over_read_length = (m_flen - sizeof(m_frame.ext.mask_0x7E));
                return state_recv_mask_0x7E;
            } else {
                if (m_flen < sizeof(m_frame.ext.mask)) { return state_recv_frame; }
                over_read_length = (m_flen - sizeof(m_frame.ext.mask));
                return state_recv_mask;
            }
        } else {
            if (m_frame.ext.h.payload_len() == 0x7F) {
                if (m_flen < sizeof(m_frame.ext.nomask_0x7F)) { return state_recv_frame; }
                over_read_length = (m_flen - sizeof(m_frame.ext.nomask_0x7F));
                return state_recv_0x7F;
            } else if (m_frame.ext.h.payload_len() == 0x7E) {
                if (m_flen < sizeof(m_frame.ext.nomask_0x7E)) { return state_recv_frame; }
                over_read_length = (m_flen - sizeof(m_frame.ext.nomask_0x7E));
                return state_recv_0x7E;
            } else {
                if (m_flen < sizeof(m_frame.ext.nomask)) { return state_recv_frame; }
                over_read_length = (m_flen - sizeof(m_frame.ext.nomask));
                return state_recv;
            }
        }
    }
    uint32_t WebSocketFSM::get_mask() {
        switch (get_state()) {
        case state_recv_mask: return GET_32(m_frame.ext.mask.masking_key);
        case state_recv_mask_0x7E: return GET_32(m_frame.ext.mask_0x7E.masking_key);
        case state_recv_mask_0x7F: return GET_32(m_frame.ext.mask_0x7F.masking_key);
        default: ASSERT(false); return 0;
        }
    }
    size_t WebSocketFSM::frame_size() {
        switch (get_state()) {
        case state_recv_mask: return m_frame.ext.h.payload_len();
        case state_recv_mask_0x7E: return ntohs(m_frame.ext.mask_0x7E.ext_payload_len);
        case state_recv_mask_0x7F: return ntohll(GET_64(m_frame.ext.mask_0x7F.ext_payload_len));
        case state_recv: return m_frame.ext.h.payload_len();
        case state_recv_0x7E: return ntohs(m_frame.ext.nomask_0x7E.ext_payload_len);
        case state_recv_0x7F: return ntohll(GET_64(m_frame.ext.nomask_0x7F.ext_payload_len));
        default: ASSERT(false); return 0;
        }
    }
    int WebSocketFSM::drain_recv_data(SessionFactory::Session &s, bool &finished) {
        int r; size_t remain = frame_size() - m_read, n_read = 0;
        analyze_frame(n_read);
        if (n_read > 0) {
            Syscall::MemCopy(m_ctrl_frame.m_buff, m_frame_buff + (m_flen - n_read), n_read);
            m_ctrl_frame.m_len += n_read;
        }
        while (remain > 0) {
            if ((r = m_ctrl_frame.drain(*this, s, remain)) <= 0) { return r; }
            m_read += r;
            remain -= r;
        }
        finished = (remain <= 0);
        return QRPC_OK;
    }
    int WebSocketFSM::read_frame(SessionFactory::Session &s, char *p, size_t l) {
        int r; size_t remain, n_read; char *orgp = p;
    retry:
        switch(get_state()) {
        case state_established:
            init_frame();
        case state_recv_frame: {
            if ((r = read_body_and_fd(s, m_frame_buff + m_flen, sizeof(Frame) - m_flen)) <= 0) {
                if (r == 0) { return r; }
                if (Syscall::EAgain()) { goto again; }
                goto error;
            }
            m_flen += r;
            m_state = analyze_frame(n_read);
            if (m_state <= state_recv_frame) { goto again; }
            if (n_read > 0) {
                if (l < n_read) { return QRPC_ESIZE; }
                Syscall::MemCopy(p, m_frame_buff + (m_flen - n_read), n_read);
                if (m_frame.masked()) { mask_payload(p, n_read, get_mask(), m_mask_idx); }
                p += n_read; l -= n_read; m_read += n_read;
            }
        }
        case state_recv_mask:
        case state_recv_mask_0x7E:
        case state_recv_mask_0x7F:
        case state_recv:
        case state_recv_0x7E:
        case state_recv_0x7F: {
            switch (m_frame.get_opcode()) {
            case opcode_continuation_frame:
            case opcode_text_frame:
            case opcode_binary_frame:
                remain = frame_size() - m_read;
                if (remain <= 0) {
                    if (m_read <= 0) { goto error; }
                    m_state = state_established;
                    goto retry;
                }
                n_read = (l < remain ? l : remain);
                if ((r = read_body_and_fd(s, p, n_read)) <= 0) {
                    if (r == 0) { return r; }
                    if (Syscall::EAgain()) { goto again; }
                    goto error;
                }
                if (m_frame.masked()) { mask_payload(p, r, get_mask(), m_mask_idx); }
                m_read += r; p += r; l -= r;
                break;
            case opcode_connection_close: {
                bool finished;
                if ((r = drain_recv_data(s, finished)) <= 0) {
                    if (r == 0) { return r; }
                    if (Syscall::EAgain()) { goto again; }
                    goto error;
                }
                if (finished) {
                    if (m_frame.masked()) { mask_payload(m_ctrl_frame.m_buff, m_ctrl_frame.m_len, get_mask(), m_mask_idx); }
                    auto code = GET_16(m_ctrl_frame.m_buff);
                    m_ctrl_frame.reset();
                    s.Close(QRPC_CLOSE_REASON_REMOTE, code, "websocket close frame received");
                }
            } break;
            case opcode_ping:
            case opcode_pong: {
                bool finished;
                if ((r = drain_recv_data(s, finished)) <= 0) {
                    if (r == 0) { return r; }
                    if (Syscall::EAgain()) { goto again; }
                    goto error;
                }
                if (finished) {
                    if (m_frame.get_opcode() == opcode_ping) {
                        if (m_frame.masked()) { mask_payload(m_ctrl_frame.m_buff, m_ctrl_frame.m_len, get_mask(), m_mask_idx); }
                        write_frame(s, m_ctrl_frame.m_buff, m_ctrl_frame.m_len, opcode_pong, m_frame.masked());
                    }
                    m_ctrl_frame.reset();
                }
            } break;
            }
            if (l > 0) { goto retry; }
            return p - orgp;
        }
        default:
            ASSERT(false);
            return QRPC_EINVAL;
        }
    again:
        return (orgp < p) ? (p - orgp) : QRPC_EAGAIN;
    error:
        return QRPC_EINVAL;
    }
    int WebSocketFSM::write_frame(SessionFactory::Session &s, const char *p, size_t l, opcode opc, bool masked, bool fin) {
        char buff[sizeof(Frame)]; uint32_t rnd = 0; uint8_t idx = 0;
        Frame *pf = reinterpret_cast<Frame *>(buff); size_t hl; Frame frm;
        pf->ext.h.set_controls(fin, masked, opc);
        if (l >= 0x7E) {
            if (l <= 0xFFFF) {
                pf->ext.h.set_payload_len(0x7E);
                if (pf->ext.h.mask()) {
                    rnd = random::gen32();
                    pf->ext.mask_0x7E.ext_payload_len = htons(l);
                    SET_32(pf->ext.mask_0x7E.masking_key, rnd);
                    hl = sizeof(frm.ext.mask_0x7E);
                } else {
                    pf->ext.nomask_0x7E.ext_payload_len = htons(l);
                    hl = sizeof(frm.ext.nomask_0x7E);
                }
            } else {
                pf->ext.h.set_payload_len(0x7F);
                if (pf->ext.h.mask()) {
                    rnd = random::gen32();
                    SET_64(pf->ext.mask_0x7F.ext_payload_len, htonll(l));
                    SET_32(pf->ext.mask_0x7F.masking_key, rnd);
                    hl = sizeof(frm.ext.mask_0x7F);
                } else {
                    SET_64(pf->ext.nomask_0x7F.ext_payload_len, htonll(l));
                    hl = sizeof(frm.ext.nomask_0x7F);
                }
            }
        } else {
            pf->ext.h.set_payload_len(l);
            if (pf->ext.h.mask()) {
                rnd = random::gen32();
                SET_32(pf->ext.mask.masking_key, rnd);
                hl = sizeof(frm.ext.mask);
            } else {
                hl = sizeof(frm.ext.nomask);
            }
        }
        if (s.Write(buff, hl) < 0) { return QRPC_ESYSCALL; }
        int r = (masked ? s.Write(mask_payload(const_cast<char *>(p), l, rnd, idx), l) : s.Write(p, l));
        if (r < 0 || ((size_t)r) < l) { return QRPC_ESYSCALL; }
        return r;
    }
    char *WebSocketFSM::init_accept_key_from_header(char *accept_key, size_t accept_key_len) {
        char kbuf[256]; int kblen;
        if (!m_sm.hdrstr("Sec-Websocket-Key", kbuf, sizeof(kbuf), &kblen)) { return nullptr; }
        uint8_t vbuf[256];
        if (sizeof(m_key_ptr) != base64::decode(kbuf, kblen, vbuf, sizeof(vbuf))) { return nullptr; }
        Syscall::MemCopy(m_key_ptr, vbuf, sizeof(m_key_ptr));
        return generate_accept_key(accept_key, accept_key_len, kbuf);
    }
    char *WebSocketFSM::generate_accept_key_from_value(char *accept_key, size_t accept_key_len) {
        char enc[base64::buffsize(sizeof(m_key_ptr))];
        base64::encode(m_key_ptr, sizeof(m_key_ptr), enc, sizeof(enc));
        return generate_accept_key(accept_key, accept_key_len, enc);
    }
    char *WebSocketFSM::generate_accept_key(char *accept_key, size_t accept_key_len, const char *sec_key) {
        if (accept_key_len < base64::buffsize(sha1::kDigestSize)) { ASSERT(false); return nullptr; }
        char work[256];
        char salt[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        size_t l = str::Vprintf(work, sizeof(work), "%s%s", sec_key, salt);
        const uint8_t *digest = sha1::digest(work, l);
        base64::encode(digest, sha1::kDigestSize, accept_key, sizeof(accept_key));
        return accept_key;
    }
    #define HS_CHECK(cond, ...) if (!(cond)) { TRACE(__VA_ARGS__); return QRPC_EINVAL; }
    int WebSocketFSM::verify_handshake() {
        char tok[256];
        HS_CHECK(m_sm.hdrstr("Upgrade", tok, sizeof(tok)), "Upgrade header\n");
        HS_CHECK(str::CmpNocase(tok, "websocket", sizeof(tok)) == 0, "Upgrade invalid %s\n", tok);
        HS_CHECK(m_sm.hdrstr("Connection", tok, sizeof(tok)), "Connection header\n");
        HS_CHECK(str::CmpNocase(tok, "upgrade", sizeof(tok)) == 0, "Connection invalid %s\n", tok);
        switch (get_state()) {
        case state_client_handshake_2: {
            char calculated[base64::buffsize(sha1::kDigestSize)];
            HS_CHECK(m_sm.rc() == HRC_SWITCHING_PROTOCOLS, "invalid response %d\n", m_sm.rc());
            HS_CHECK(m_sm.hdrstr("Sec-WebSocket-Accept", tok, sizeof(tok)) != nullptr, "Sec-WebSocket-Accept header\n");
            HS_CHECK(nullptr != generate_accept_key_from_value(calculated, sizeof(calculated)), "cannot calculate accept key from client data\n");
            HS_CHECK(str::CmpNocase(tok, calculated, sizeof(calculated)) == 0, "Sec-WebSocket-Accept Invalid\n");
        } return QRPC_OK;
        case state_server_handshake: {
            int v;
            HS_CHECK(m_sm.hashdr("Host"), "Host header\n");
            HS_CHECK(m_sm.hashdr("Sec-WebSocket-Key"), "Sec-WebSocket-Key header\n");
            HS_CHECK(m_sm.hdrint("Sec-WebSocket-version", v) >= 0, "Sec-WebSocket-version header\n");
            HS_CHECK(v == 13, "version invalid %u\n", v);
        } return QRPC_OK;
        default:
            ASSERT(false);
            return QRPC_EINVAL;
        }
    }
    int WebSocketFSM::send_handshake_request(SessionFactory::Session &s, const char *host) {
        init_key();
        char out[base64::buffsize(sizeof(m_key_ptr))], origin[256];
        base64::encode(m_key_ptr, sizeof(m_key_ptr), out, sizeof(out));
        str::Vprintf(origin, sizeof(origin), "http://%s", host);
        auto r = WebSocketListener::send_handshake_request(s, host, out, origin, NULL);
        if (r < 0) { return Syscall::IOMayBlocked(r, false) ? QRPC_EAGAIN : QRPC_ESYSCALL; }
        return r;
    }
    int WebSocketFSM::send_handshake_response(SessionFactory::Session &s) {
        char buffer[base64::buffsize(sha1::kDigestSize)], *p;
        if (!(p = init_accept_key_from_header(buffer, sizeof(buffer)))) { return QRPC_EINVAL; }
        auto r = WebSocketListener::send_handshake_response(s, buffer);
        if (r < 0) { return Syscall::IOMayBlocked(r, false) ? QRPC_EAGAIN : QRPC_ESYSCALL; }
        return r;
    }
    int WebSocketFSM::handshake(SessionFactory::Session &s, int r, int w) {
        char rbf[4096]; int rsz;
        switch (get_state()) {
        case state_client_handshake:
            if (!w) { return QRPC_EAGAIN; }
            if (send_handshake_request(s, m_hostname.c_str()) < 0) {
                return Syscall::EAgain() ? QRPC_EAGAIN : QRPC_ESYSCALL;
            }
            set_state(state_client_handshake_2);
            return QRPC_EAGAIN;
        case state_client_handshake_2:
        case state_server_handshake:
            if (!r) { return QRPC_EAGAIN; }
            if ((rsz = s.Read(rbf, sizeof(rbf))) < 0) {
                return Syscall::EAgain() ? QRPC_EAGAIN : QRPC_ESYSCALL;
            }
            switch (m_sm.append(rbf, rsz)) {
            case HttpFSM::state_recv_header:
                return QRPC_EAGAIN;
            case HttpFSM::state_websocket_establish:
                if (verify_handshake() < 0) { return QRPC_EINVAL; }
                if (get_state() == state_server_handshake && send_handshake_response(s) < 0) {
                    return Syscall::EAgain() ? QRPC_EAGAIN : QRPC_ESYSCALL;
                }
                set_state(state_established);
                return QRPC_OK;
            default:
                ASSERT(false);
                return QRPC_EINVAL;
            }
        default:
            ASSERT(false);
            return QRPC_EINVAL;
        }
    }
    #undef HS_CHECK
}
