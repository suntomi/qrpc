load("@rules_foreign_cc//foreign_cc:defs.bzl", "make")

# this cannot work on OSX because wrapped version of libtool 
# in bazel sandbox does not support --version option, which is necessary for meson.
# I had to build mediasoup separately in makefile and use it as cc_import.
# TODO: fix this

# make(
#   name = "mediasoup",
#   lib_source = "//src/ext/mediasoup:srcs",
#   targets = ["libmediasoup-worker"],
#   out_include_dir = "worker/include",
#   out_static_libs = ["worker/out/Release/libmediasoup_worker.a"],
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
  srcs = ["examples/server/main.cpp"],
  includes = [
    "src/ext/mediasoup/worker/include",
    "src/ext/mediasoup/worker/subprojects/abseil-cpp-20220623.0",
    "src/ext/mediasoup/worker/subprojects/nlohmann_json-3.10.5/include",
    "src/ext/mediasoup/worker/subprojects/libuv-v1.44.2/include",
  ],
  deps = [":mediasoup"],
  linkstatic = True,
)
