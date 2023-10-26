#pragma once

#include <cstdint>
#include <cstring>

namespace base {
  namespace random {
    thread_local std::mt19937 engine(std::random_device{}());
    template <class N>
    inline N gen(N min, N max) {
      std::uniform_int_distribution<N> dist(min, max);
      return dist(engine);
    }
    inline uint32_t gen32() {
      return gen<uint32_t>(0, UINT32_MAX);
    }
    inline uint64_t gen64() {
      return gen<uint64_t>(0, UINT64_MAX);
    }
  }

  namespace sha1 {
    constexpr static int kDigestSize = 20;
    inline void digest(const uint8_t* data, size_t len, uint8_t* digest) {
      SHA1 sha1;
      sha1.Update(data, len);
      sha1.Final(digest);
    }
    class SHA1 {
    public:
      SHA1() { Init(); }

      void Update(const uint8_t* data, size_t len) {
        size_t i;

        for (i = 0; i < len; ++i) {
          buffer_[buffer_pos_++] = data[i];
          if (buffer_pos_ == 64) {
            Transform();
          }
        }
      }

      void Final(uint8_t* digest) {
        uint64_t total_bits = count_ * 8;
        buffer_[buffer_pos_++] = 0x80;

        if (buffer_pos_ > 56) {
          while (buffer_pos_ < 64) {
            buffer_[buffer_pos_++] = 0;
          }
          Transform();
          buffer_pos_ = 0;
        }

        while (buffer_pos_ < 56) {
          buffer_[buffer_pos_++] = 0;
        }

        buffer_[56] = (total_bits >> 56) & 0xff;
        buffer_[57] = (total_bits >> 48) & 0xff;
        buffer_[58] = (total_bits >> 40) & 0xff;
        buffer_[59] = (total_bits >> 32) & 0xff;
        buffer_[60] = (total_bits >> 24) & 0xff;
        buffer_[61] = (total_bits >> 16) & 0xff;
        buffer_[62] = (total_bits >> 8) & 0xff;
        buffer_[63] = total_bits & 0xff;

        Transform();

        for (int i = 0; i < 5; ++i) {
          digest[i * 4] = (state_[i] >> 24) & 0xff;
          digest[i * 4 + 1] = (state_[i] >> 16) & 0xff;
          digest[i * 4 + 2] = (state_[i] >> 8) & 0xff;
          digest[i * 4 + 3] = state_[i] & 0xff;
        }
      }

    private:
        void Init() {
          state_[0] = 0x67452301;
          state_[1] = 0xefcdab89;
          state_[2] = 0x98badcfe;
          state_[3] = 0x10325476;
          state_[4] = 0xc3d2e1f0;

          count_ = 0;
          buffer_pos_ = 0;
        }

        void Transform() {
          uint32_t a = state_[0];
          uint32_t b = state_[1];
          uint32_t c = state_[2];
          uint32_t d = state_[3];
          uint32_t e = state_[4];

          uint32_t w[80];
          for (int i = 0; i < 16; ++i) {
            w[i] = (buffer_[i * 4] << 24) |
                  (buffer_[i * 4 + 1] << 16) |
                  (buffer_[i * 4 + 2] << 8) |
                  (buffer_[i * 4 + 3]);
          }
          for (int i = 16; i < 80; ++i) {
            w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
          }

          for (int i = 0; i < 80; ++i) {
              uint32_t f, k;
              if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999;
              } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
              } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
              } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
              }

              uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
              e = d;
              d = c;
              c = RotateLeft(b, 30);
              b = a;
              a = temp;
          }

          state_[0] += a;
          state_[1] += b;
          state_[2] += c;
          state_[3] += d;
          state_[4] += e;

          buffer_pos_ = 0;
          count_ += 64;
        }

        uint32_t RotateLeft(uint32_t value, int shift) {
          return (value << shift) | (value >> (32 - shift));
        }

        uint32_t state_[5];
        uint8_t buffer_[64];
        uint64_t count_;
        int buffer_pos_;
    };
  }
  namespace base64 {
      static const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

      int buffsize(int len) {
        return (len + 2) / 3 * 4 + 1;
      }

      int encode(const uint8_t* src, int len, char* dst, int dstlen) {
        int i, j;
        uint8_t a, b, c;
        char* p = dst;

        for (i = 0; i < len; i += 3) {
          a = src[i];
          b = (i + 1 < len) ? src[i + 1] : 0;
          c = (i + 2 < len) ? src[i + 2] : 0;

          *p++ = kBase64Chars[a >> 2];
          *p++ = kBase64Chars[((a & 0x03) << 4) | (b >> 4)];
          *p++ = (i + 1 < len) ? kBase64Chars[((b & 0x0f) << 2) | (c >> 6)] : '=';
          *p++ = (i + 2 < len) ? kBase64Chars[c & 0x3f] : '=';
        }

        *p = '\0';

        return p - dst;
      }

      int decode(const char* src, int len, uint8_t* dst, int dstlen) {
        int i, j;
        uint8_t a, b, c, d;
        uint8_t* p = dst;

        for (i = 0; i < len; i += 4) {
          a = (src[i] == '=') ? 0 : strchr(kBase64Chars, src[i]) - kBase64Chars;
          b = (src[i + 1] == '=') ? 0 : strchr(kBase64Chars, src[i + 1]) - kBase64Chars;
          c = (src[i + 2] == '=') ? 0 : strchr(kBase64Chars, src[i + 2]) - kBase64Chars;
          d = (src[i + 3] == '=') ? 0 : strchr(kBase64Chars, src[i + 3]) - kBase64Chars;

          *p++ = (a << 2) | (b >> 4);
          if (i + 2 < len && src[i + 2] != '=') {
              *p++ = ((b & 0x0f) << 4) | (c >> 2);
          }
          if (i + 3 < len && src[i + 3] != '=') {
              *p++ = ((c & 0x03) << 6) | d;
          }
        }

        return p - dst;
      }
  }    
}