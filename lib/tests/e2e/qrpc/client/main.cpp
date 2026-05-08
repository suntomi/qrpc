#include "client.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <sctp|stcp|rtp>\n", argv[0]);
    return 2;
  }
  if (std::strcmp(argv[1], "sctp") == 0 || std::strcmp(argv[1], "stcp") == 0) {
    return test_sctp_client();
  }
  if (std::strcmp(argv[1], "rtp") == 0) {
    return test_rtp_client();
  }
  std::fprintf(stderr, "unknown qrpc e2e client test: %s\n", argv[1]);
  return 2;
}
