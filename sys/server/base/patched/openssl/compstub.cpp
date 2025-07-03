#include <stddef.h>
#include <openssl/comp.h>
#include <cstdlib>

#include "base/logger.h"
#include "base/macros.h"

// C++ wrapper to call logger::tracef with fatal level and exit
static void die_with_message(const char* fn) {
  base::logger::die({{"ev","compstub API called"},{"fn",fn}});
}

extern "C" {
// OpenSSL compression stub implementations for QRPC
// Used when OpenSSL is built without compression support

// due to meson-provided openssl build bug (OPENSSL_NO_COMP is not defined but comp/*.c is not built)
// because openssl/include/openssl/configuration.h is used for the build
// should use generated-config/archs/$ARCH/configuration.h instead but -I order does not allow it

// because reference to COMP_CTX_xxx is removed by DCE, its ok.

// Stub implementation - compression not supported
COMP_CTX *COMP_CTX_new(COMP_METHOD *meth) {
  die_with_message(__func__);
  return NULL;
}

// Stub implementation - compression not supported
void COMP_CTX_free(COMP_CTX *ctx) {
  die_with_message(__func__);
}

// Stub implementation - compression not supported
const COMP_METHOD *COMP_CTX_get_method(const COMP_CTX *ctx) {
  die_with_message(__func__);
  return NULL;
}

// Additional stubs for other missing compression functions
COMP_METHOD *COMP_zlib(void) {
  die_with_message(__func__);
  return NULL;
}

int COMP_compress_block(COMP_CTX *ctx, unsigned char *out, int olen,
                        unsigned char *in, int ilen) {
  die_with_message(__func__);
  return -1;
}

int COMP_expand_block(COMP_CTX *ctx, unsigned char *out, int olen,
                      unsigned char *in, int ilen) {
  die_with_message(__func__);
  return -1;
}

// Get compression method name
const char *COMP_get_name(const COMP_METHOD *meth) {
  die_with_message(__func__);
  return NULL;
}

// Get compression method type/id
int COMP_get_type(const COMP_METHOD *meth) {
  die_with_message(__func__);
  return -1;
}

// OpenSSL internal function for zlib cleanup
void ossl_comp_zlib_cleanup(void) {
  die_with_message(__func__);
}

// Load compression error strings
int ossl_err_load_COMP_strings(void) {
  die_with_message(__func__);
  return 0;
}
}