# load("@rules_foreign_cc//foreign_cc:defs.bzl", "make")
load("//tools/bazel/libs:setup_targets.bzl", "setup_targets")

setup_targets()

load("//tools/bazel/libs:selects.bzl", "selects")

load("//tools/bazel/libs:ms_cppargs.bzl", "MS_CPPARGS")

# this cannot work on OSX because wrapped version of libtool 
# in bazel sandbox does not support --version option, which is necessary for meson.
# I had to build mediasoup separately in makefile and use it as cc_import.
# TODO: fix this

# make(
#   name = "mediasoup",
#   lib_source = "//src/ext/mediasoup:srcs",
#   targets = ["libmediasoup-worker"],
#   out_include_dir = "src/ext/mediasoup/worker/include",
#   out_static_libs = ["src/ext/mediasoup/worker/out/Release/libmediasoup_worker.a"],
#   # without this tag, the build will fail with: 
#   # Failed to establish a new connection: [Errno 1] Operation not permitted')': /simple/meson/
#   # because of network access is sandboxed by default
#   tags = {"requires-network": 1}
# )

cc_import(
  name = "mediasoup",
  hdrs = glob(["src/ext/mediasoup/worker/include/**", "src/ext/mediasoup/worker/subprojects/**"]),
  static_library = "src/ext/mediasoup/worker/out/Release/libmediasoup-worker.a"
)

cc_binary(
  name = "server",
  srcs = glob([
    "examples/server/main.cpp",
    "src/qrpc.h", "src/qrpc.cpp",
    "src/base/**",
    "src/ext/mediasoup/include/*.hpp",
    "src/ext/mediasoup/include/**/*.hpp",
    "src/ext/moodycamel/*.h",
    "src/ext/hedley/*.h",
    "src/ext/sha1/*.h"
  ]),
  copts = [
    "-std=c++17",
  ] 
  + MS_CPPARGS 
  + selects.with_or({
    (
      ":ios_x86_64", ":ios_armv7", ":ios_armv7s", ":ios_arm64", ":ios_sim_arm64",
      ":tvos_x86_64", ":tvos_arm64",
      ":watchos_i386", ":watchos_x86_64", ":watchos_armv7k", ":watchos_arm64_32",
      ":darwin", ":darwin_x86_64", ":darwin_arm64", ":darwin_arm64e",
      ":openbsd"
    ): [
      "-D__ENABLE_KQUEUE__",
    ],
    ":windows": [
      "-D__ENABLE_IOCP__",
    ],
    (":android", "//conditions:default"): [
      "-D__ENABLE_EPOLL__", "-D__QRPC_USE_RECVMMSG__"
    ],
  }),
  includes = [
    "src",
    "src/ext",
    "src/ext/mediasoup/worker/include",
    "src/ext/mediasoup/worker/subprojects/abseil-cpp-20220623.0",
    "src/ext/mediasoup/worker/subprojects/nlohmann_json-3.10.5/include",
    "src/ext/mediasoup/worker/subprojects/libuv-v1.44.2/include",
    "src/ext/mediasoup/worker/subprojects/libsrtp-2.5.0/include",
    "src/ext/mediasoup/worker/subprojects/openssl-3.0.8/include",
    "src/ext/mediasoup/worker/subprojects/usrsctp-4e06feb01cadcd127d119486b98a4bd3d64aa1e7/usrsctplib",
  ],
  deps = [":mediasoup", "//src/ext/cares:ares"],
  linkstatic = True,
)
