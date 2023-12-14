#pragma once

#include "base/endian.h"
#include "base/logger.h"
#include "base/stream.h"
#include <stdint.h>

namespace base {
  enum PPID {
    WEBRTC_DCEP = 50,
    STRING = 51,
    BINARY_PARTIAL = 52, //deprecated
    BINARY = 53,
    STRING_PARTIAL = 54, //deprecated
    STRING_EMPTY = 56,
    BINARY_EMPTY = 57,
  };
  enum DcepMessageType : uint8_t {
    DATA_CHANNEL_ACK = 2,
    DATA_CHANNEL_OPEN = 3,
  };
  enum DcepChannelType : uint8_t {
    // The data channel provides a reliable in-order bidirectional communication.
    DATA_CHANNEL_RELIABLE = 0x00,
    // The data channel provides a reliable unordered bidirectional communication.
    DATA_CHANNEL_RELIABLE_UNORDERED = 0x80,
    // The data channel provides a partially reliable in-order bidirectional communication. 
    // User messages will not be retransmitted more times than specified in the Reliability Parameter.
    DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT = 0x01,
    // The data channel provides a partially reliable unordered bidirectional communication. 
    // User messages will not be retransmitted more times than specified in the Reliability Parameter.
    DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED = 0x81,
    // The data channel provides a partially reliable in-order bidirectional communication. 
    // User messages might not be transmitted or retransmitted after a specified lifetime given in milliseconds in the Reliability Parameter. 
    // This lifetime starts when providing the user message to the protocol stack.    
    DATA_CHANNEL_PARTIAL_RELIABLE_TIMED = 0x02,
    // The data channel provides a partially reliable unordered bidirectional communication. 
    // User messages might not be transmitted or retransmitted after a specified lifetime given in milliseconds in the Reliability Parameter. 
    // This lifetime starts when providing the user message to the protocol stack. 
    DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED = 0x82
  };
  class DcepRequest : public Stream::Config {
  public:
    typedef struct {
      DcepMessageType msg_type;
      DcepChannelType channel_type;
      uint16_t priority;
      uint32_t reliability_params;
      uint16_t label_length;
      uint16_t protocol_length;
    } Header;
  public:
    DcepRequest(
      const Stream::Config &c, uint16_t prio = 256
    ) : Stream::Config(c), msg_type(DATA_CHANNEL_OPEN), channel_type(ToChannelType(c)), priority(prio) {}
    DcepRequest() : Stream::Config(), msg_type(DATA_CHANNEL_OPEN), channel_type(DATA_CHANNEL_RELIABLE), priority(256) {}
    inline const Stream::Config &ToStreamConfig() const { return *this; }
  public:
    static DcepChannelType ToChannelType(const Stream::Config &c) {
      if (c.params.ordered) {
        if (c.params.maxPacketLifeTime != 0) {
          return DATA_CHANNEL_PARTIAL_RELIABLE_TIMED;
        } else if (c.params.maxRetransmits != 0) {
          return DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT;
        } else {
          return DATA_CHANNEL_RELIABLE;
        }
      } else {
        if (c.params.maxPacketLifeTime != 0) {
          return DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED;
        } else if (c.params.maxRetransmits != 0) {
          return DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED;
        } else {
          return DATA_CHANNEL_RELIABLE_UNORDERED;
        }
      }
    }
    static std::unique_ptr<DcepRequest> Parse(uint16_t stream_id, const uint8_t *p, size_t len) {
      auto r = std::make_unique<DcepRequest>();
      const Header *h = reinterpret_cast<const Header *>(p);
      r->params.streamId = stream_id;
      r->msg_type = h->msg_type;
      if (r->msg_type != DATA_CHANNEL_OPEN) {
        // because we already check msg type before calling the method, this should not happen
        logger::error({{"ev","invalid dcep packet"},{"reason", "invalid msg type"},{"msg_type",r->msg_type}});
        return nullptr;
      }
      r->channel_type = h->channel_type;
      auto rp = Endian::NetToHost(h->reliability_params);
      if ((r->channel_type & 0x7f) == DATA_CHANNEL_RELIABLE && rp != 0) {
        logger::error({{"ev","invalid dcep packet"},{"reason", "reliability_params must be 0 for reliable channel"}});
        return nullptr;
      }
      r->params.ordered = (r->channel_type & 0x80) == 0;
      switch (r->channel_type & 0x7f) {
        case DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT:
          r->params.maxPacketLifeTime = 0;
          r->params.maxRetransmits = rp;
          break;
        case DATA_CHANNEL_PARTIAL_RELIABLE_TIMED:
          r->params.maxPacketLifeTime = rp;
          r->params.maxRetransmits = 0;
          break;
        default:
          r->params.maxPacketLifeTime = 0;
          r->params.maxRetransmits = 0;
          break;
      }
      r->priority = Endian::NetToHost(h->priority);
      auto llen = Endian::NetToHost(h->label_length);
      auto plen = Endian::NetToHost(h->protocol_length);
      if (h->label_length > 0) {
        r->label.assign(reinterpret_cast<const char *>(p) + sizeof(Header), llen);
      }
      if (h->protocol_length > 0) {
        r->protocol.assign(reinterpret_cast<const char *>(p) + sizeof(Header) + llen, plen);
        logger::error({{"ev","invalid dcep packet"},{"reason", "protocol field of dcep request is not supported"},{"proto",r->protocol}});
        return nullptr;
      }
      return r;
    }
    const uint8_t *ToPaylod(uint8_t *buffer, size_t sz) {
      if (sz < PayloadSize()) {
        logger::error({{"ev","invalid dcep packet"},{"reason", "buffer size is too small"},{"sz",sz},{"payload_size",PayloadSize()}});
        return nullptr;
      }
      Header *h = reinterpret_cast<Header *>(buffer);
      h->msg_type = msg_type;
      h->channel_type = channel_type;
      h->priority = Endian::HostToNet(priority);
      switch (channel_type & 0x7f) {
        case DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT:
          h->reliability_params = Endian::HostToNet(params.maxRetransmits);
          break;
        case DATA_CHANNEL_PARTIAL_RELIABLE_TIMED:
          h->reliability_params = Endian::HostToNet(params.maxPacketLifeTime);
          break;
        default:
          h->reliability_params = 0;
          break;
      }
      h->label_length = Endian::HostToNet(label.length());
      h->protocol_length = Endian::HostToNet(protocol.length());
      Syscall::MemCopy(buffer + sizeof(Header), label.c_str(), label.length());
      Syscall::MemCopy(buffer + sizeof(Header) + label.length(), protocol.c_str(), protocol.length());
      return buffer;
    }
    inline size_t PayloadSize() const {
      return sizeof(Header) + label.length() + protocol.length();
    }
  private:
    DcepMessageType msg_type;
    DcepChannelType channel_type;
    uint16_t priority;
  };
  class DcepResponse {
  public:
    const uint8_t *ToPaylod(uint8_t *buffer, size_t sz) {
      if (sz < PayloadSize()) {
        logger::error({{"ev","invalid dcep packet"},{"reason", "buffer size is too small"},{"sz",sz},{"payload_size",PayloadSize()}});
        return nullptr;
      }
      buffer[0] = DATA_CHANNEL_ACK;
      return buffer;
    }
    inline size_t PayloadSize() const {
      return 1;
    }
  };
}

