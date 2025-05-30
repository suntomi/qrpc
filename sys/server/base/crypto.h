#pragma once

#include <cstdint>
#include <cstring>
#include <random>
#include "sha1/sha1.h"

#include "base/syscall.h"

namespace base {
  namespace random {
    std::mt19937 &prng();
    template <class N>
    inline N gen(N min, N max) {
      std::uniform_int_distribution<N> dist(min, max);
      return dist(prng());
    }
    template <>
    inline float gen<float>(float min, float max) {
      std::uniform_real_distribution<float> dist(min, max);
      return dist(prng());
    }
    template <>
    inline double gen<double>(double min, double max) {
      std::uniform_real_distribution<double> dist(min, max);
      return dist(prng());
    }
    inline float genf32() {
      return gen<float>(0.0f, 1.0f);
    }
    inline double genf64() {
      return gen<double>(0.0, 1.0);
    }
    inline uint32_t gen32() {
      return gen<uint32_t>(0, UINT32_MAX);
    }
    inline uint64_t gen64() {
      return gen<uint64_t>(0, UINT64_MAX);
    }
    inline uint8_t *bytes(uint8_t *ret, size_t len) {
      // generate random bytes from std::mt19937
      for (size_t i = 0; i < len; ++i) {
        // TODO: should use uint64_t and cast to uint8_t*?
        ret[i] = gen<uint8_t>(0, UINT8_MAX);
      }
      return ret;
    }
    inline std::string str(size_t len) {
      // generate random string (visible characters) from std::mt19937
      std::string ret(len, 0);
      for (size_t i = 0; i < len; ++i) {
        ret[i] = gen((uint8_t)'!', (uint8_t)'~');
      }
      return ret;
    }
    inline std::string word(size_t len) {
      // generate random string (alphanumeric) from std::mt19937
      static const char alnums[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
      std::string ret(len, 0);
      for (size_t i = 0; i < len; ++i) {
        // sizeof(alnums) - 2 to exclude null terminator
        auto idx = gen((uint8_t)0, (uint8_t)(sizeof(alnums) - 2));
        ret[i] = alnums[idx];
        ASSERT(ret[i] != 0);
      }
      return ret;
    }
  }

  namespace sha1 {
    constexpr static int kDigestSize = 20;
    inline const uint8_t *digest(const uint8_t* data, size_t len) {
      static thread_local SHA1 sha1;
      sha1.reset();
      sha1.update(data, len);
      return sha1.final();
    }
    inline const uint8_t *digest(const char *data, size_t len) {
      return digest(reinterpret_cast<const uint8_t*>(data), len);
    }
  }
  namespace base64 {
    static const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    inline int buffsize(int len) {
      return (len + 2) / 3 * 4 + 1;
    }

    inline int encode(const uint8_t* src, int len, char* dst, int dstlen) {
      int i;
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

    inline int decode(const char* src, int len, uint8_t* dst, int dstlen) {
      int i;
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
  namespace cert {
    std::string gen(
      std::pair<std::string, std::string> &pair,
      const std::vector<std::string>& hostnames,
      int validityDays = 365,
      int bits = 2048
    );
  }
}