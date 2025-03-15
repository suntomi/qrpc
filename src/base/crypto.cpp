#include "base/crypto.h"

#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>

namespace base {
  namespace random {
    thread_local std::mt19937 engine(std::random_device{}());
    std::mt19937 &prng() { return engine; }
  }
  namespace cert {
    // TODO: support private key other than rsa
    static inline bool new_pkey(EVP_PKEY **pkey, int bits, std::string &err) {
      *pkey = nullptr;
      // EVP_PKEYコンテキスト作成
      EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
      if (!ctx) {
        err = "Failed to create EVP_PKEY_CTX";
        goto end;
      }
      // RSAキーペア生成の初期化
      if (EVP_PKEY_keygen_init(ctx) <= 0) {
        err = "Failed to initialize RSA key generation";
        goto end;
      }
      // RSAキーのビット長設定
      if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0) {
        err = "Failed to set RSA key length";
        goto end;
      }
      // RSAキーペア生成
      if (EVP_PKEY_keygen(ctx, pkey) <= 0) {
        err = "Failed to generate RSA key pair";
        goto end;
      }
    end:
      EVP_PKEY_CTX_free(ctx);
      return *pkey != nullptr;
    }
    // シリアル番号をランダムに設定
    static inline bool set_serial_number(X509 *x509, std::string &err) {
      ASN1_INTEGER* serialNumber = ASN1_INTEGER_new();
      BIGNUM* bn = BN_new();
      if (!serialNumber) {
        err = "Failed to create serial number";
        goto end;
      }
      if (!bn) {
        err = "Failed to create BIGNUM";
        goto end;
      }
      if (BN_pseudo_rand(bn, 64, 0, 0) != 1) {
        err = "Failed to generate random serial number";
        goto end;
      }
      if (BN_to_ASN1_INTEGER(bn, serialNumber) == nullptr) {
        err = "Failed to convert BIGNUM to ASN1_INTEGER";
        goto end;
      }
      BN_free(bn);
      if (X509_set_serialNumber(x509, serialNumber) != 1) {
        err = "Failed to set serial number";
        goto end;
      }
    end:
      if (serialNumber) ASN1_INTEGER_free(serialNumber);
      if (bn) BN_free(bn);
      return err.empty();
    }
    // 証明書の発行者と使用者の情報を設定
    static inline bool set_names(X509 *x509, const std::string &hostname, std::string &err) {
      X509_NAME* name = X509_get_subject_name(x509);
      if (!name) {
        err = "Failed to get subject name";
        return false;
      }
      // Common Name (CN) にホスト名を設定
      if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(hostname.c_str()), -1, -1, 0) != 1) {
        err = "Failed to set subject CN";
        return false;
      }
      if (X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("qrpc autogen"), -1, -1, 0) != 1) {
        err = "Failed to set subject O";
        return false;
      }
      // 発行者名も同じ（self-signed）
      if (X509_set_issuer_name(x509, name) != 1) {
        err = "Failed to set issuer name";
        return false;
      }
      return true;
    }
    static inline bool set_validity(X509 *x509, int validityDays, std::string &err) {
      // 有効期間の設定
      if (!X509_gmtime_adj(X509_getm_notBefore(x509), 0)) {
        err = "Failed to set notBefore";
        return false;
      }
      if (!X509_gmtime_adj(X509_getm_notAfter(x509), 60 * 60 * 24 * validityDays)) {
        err = "Failed to set notAfter";
        return false;
      }
      return true;
    }
    static inline bool set_san(X509 *x509, const std::vector<std::string> &hostnames, std::string &err) {
      // SAN (Subject Alternative Name) 拡張機能の追加
      X509V3_CTX ctx;
      X509V3_set_ctx_nodb(&ctx);
      X509V3_set_ctx(&ctx, x509, x509, nullptr, nullptr, 0);
      
      std::string san;
      for (size_t i = 0; i < hostnames.size(); i++) {
          if (i > 0) san += ",";
          san += "DNS:" + hostnames[i];
      }
      X509_EXTENSION* ext = X509V3_EXT_conf_nid(
        nullptr, &ctx, NID_subject_alt_name, 
        const_cast<char*>(san.c_str()));
      if (!ext) {
        err = "Failed to create SAN extension";
        goto end;
      }
      if (X509_add_ext(x509, ext, -1) != 1) {
        err = "Failed to add SAN extension";
        goto end;
      }
      X509_EXTENSION_free(ext);
      
      // Basic Constraints 拡張機能の追加（CA:FALSE）
      ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:FALSE");
      if (!ext) {
        err = "Failed to create Basic Constraints extension";
        goto end;
      }
      
      if (X509_add_ext(x509, ext, -1) != 1) {
        err = "Failed to add Basic Constraints extension";
        goto end;
      }
    end:
      if (ext) X509_EXTENSION_free(ext);
      return err.empty();
    }
    static inline bool to_string(X509 *x509, std::string &pem, std::string err) {
      BIO* bio = BIO_new(BIO_s_mem());
      BUF_MEM* bptr;
      if (!bio) {
        err = "Failed to create BIO";
        goto end;
      }
      if (PEM_write_bio_X509(bio, x509) != 1) {
        err = "Failed to write X509 to BIO";
        goto end;
      }      
      BIO_get_mem_ptr(bio, &bptr);
      pem = std::string(bptr->data, bptr->length);
    end:
      if (bio) BIO_free(bio);
      return err.empty();
    }
    static inline bool to_string(EVP_PKEY *pkey, std::string &pem, std::string err) {
      BIO* bio = BIO_new(BIO_s_mem());
      BUF_MEM* bptr;
      if (!bio) {
        err = "Failed to create BIO";
        goto end;
      }
      if (PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        err = "Failed to write X509 to BIO";
        goto end;
      }      
      BIO_get_mem_ptr(bio, &bptr);
      pem = std::string(bptr->data, bptr->length);
    end:
      if (bio) BIO_free(bio);
      return err.empty();
    }    
    std::string gen(
      std::pair<std::string, std::string> &pair,
      const std::vector<std::string>& hostnames,
      int validityDays,
      int bits
    ) {
      // 結果を格納する変数
      std::string err;      
      // ハンドルとなる変数
      EVP_PKEY* pkey = nullptr;
      X509* x509 = nullptr;
      // generate private key
      if (!new_pkey(&pkey, bits, err)) {
        return err;
      }
      // X.509証明書作成
      x509 = X509_new();
      if (!x509) { 
        err = "Failed to create X509 certificate";
        goto end;
      }        
      // 証明書のバージョンを3に設定
      X509_set_version(x509, 2); // 0 for V1, 1 for V2, 2 for V3
      // シリアル番号を設定 
      if (!set_serial_number(x509, err)) {
        goto end;
      }
      // 証明書の発行者と使用者の情報を設定
      if (!set_names(x509, hostnames[0], err)) {
        goto end;
      }
      // 有効期間の設定
      if (!set_validity(x509, validityDays, err)) {
        goto end;
      }
      // 公開鍵の設定
      if (X509_set_pubkey(x509, pkey) != 1) {
        goto end;
      }        
      // SAN (Subject Alternative Name) 拡張機能の追加
      if (!set_san(x509, hostnames, err)) {
        goto end;
      }
      // 証明書に署名
      if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
        err = "Failed to sign certificate";
        goto end;
      }
      // 証明書をPEM形式にエクスポート
      if (!to_string(x509, pair.first, err)) {
        goto end;
      }
      // 秘密鍵をPEM形式にエクスポート
      if (!to_string(pkey, pair.second, err)) {
        goto end;
      }
    end:
      if (x509) X509_free(x509);
      if (pkey) EVP_PKEY_free(pkey);
      
      return err;
    }
  }
}