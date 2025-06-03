load("//tools/bazel/libs:selects.bzl", "selects")
load("//tools/bazel/libs:ms_cppargs.bzl", "MS_CPPARGS")

cc_import(
  name = "mediasoup",
  hdrs = glob([
    "ext/mediasoup/worker/include/**",
    "ext/mediasoup/worker/subprojects/**",
    "ext/mediasoup/worker/deps/libwebrtc/**"
  ]),
  static_library = selects.with_or({
    "//:is_debug_build": "ext/mediasoup/worker/out/Debug/libmediasoup-worker.a",
    "//conditions:default": "ext/mediasoup/worker/out/Release/libmediasoup-worker.a",
  })
)

cc_library(
  name = "qrpc_server_lib",
  srcs = glob([
    "qrpc.cpp",
    "base/**/*.cpp",
    "ext/libsdptransform/src/*.cpp",
  ], exclude = [
    "**/*.md"
  ]),
  hdrs = glob([
    "qrpc.h",
    "base/**/*.h",
    "ext/moodycamel/*.h",
    "ext/hedley/*.h",
    "ext/sha1/*.h",
    "ext/libsdptransform/include/*.hpp",
    "ext/json/include/**/*.hpp",
  ]),
  copts = [
    "-std=c++17",
  ]
  + selects.with_or({
    "//:asan": ["-fsanitize=address"],
    "//conditions:default": [],
  })
  + MS_CPPARGS 
  + selects.with_or({
    (
      "//:ios_x86_64", "//:ios_armv7", "//:ios_armv7s", "//:ios_arm64", "//:ios_sim_arm64",
      "//:tvos_x86_64", "//:tvos_arm64",
      "//:watchos_i386", "//:watchos_x86_64", "//:watchos_armv7k", "//:watchos_arm64_32",
      "//:darwin", "//:darwin_x86_64", "//:darwin_arm64", "//:darwin_arm64e",
      "//:openbsd"
    ): [
      "-D__ENABLE_KQUEUE__",
    ],
    "//:windows": [
      "-D__ENABLE_UV__",
    ],
    ("//:android", "//conditions:default"): [
      "-D__ENABLE_EPOLL__", "-D__QRPC_USE_RECVMMSG__"
    ],
  }),
  includes = [
    ".",
    "ext",
    "ext/mediasoup/worker/deps/libwebrtc",
    "ext/mediasoup/worker/deps/libwebrtc/libwebrtc",
    "ext/mediasoup/worker/include",
    "ext/mediasoup/worker/subprojects/abseil-cpp-20240722.0",
    "ext/mediasoup/worker/subprojects/flatbuffers-24.3.6/include",
    "ext/mediasoup/worker/subprojects/libuv-v1.48.0/include",
    "ext/mediasoup/worker/subprojects/libsrtp-3.0-alpha/include",
    "ext/mediasoup/worker/subprojects/nlohmann_json-3.10.5/include",
    "ext/mediasoup/worker/subprojects/openssl-3.0.8/include",
    "ext/mediasoup/worker/subprojects/usrsctp-d45b53f5dfa79533f5c5e7aefa5d7570405afb39/usrsctplib",
    "ext/libsdptransform/include",
    "ext/json/include",
  ] + selects.with_or({
    "//:is_debug_build": ["ext/mediasoup/worker/out/Debug/build/fbs"],
    "//conditions:default": ["ext/mediasoup/worker/out/Release/build/fbs"],
  }),
  deps = [":mediasoup", "//sys/server/ext/cares:ares"],
  visibility = ["//visibility:public"],
)
