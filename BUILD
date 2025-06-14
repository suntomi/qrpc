# load("@rules_foreign_cc//foreign_cc:defs.bzl", "make")
load("//tools/bazel/libs:setup_targets.bzl", "setup_targets")

setup_targets()

load("//tools/bazel/libs:selects.bzl", "selects")

load("//tools/bazel/libs:ms_cppargs.bzl", "MS_CPPARGS")

config_setting(
    name = "is_debug_build",
    values = {
        "compilation_mode": "dbg",
    },
    visibility = ["//visibility:public"],
)

platform(
    name = "platform_linux_arm64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
)

platform(
    name = "platform_linux_amd64", 
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
)

platform(
    name = "platform_darwin_arm64", 
    constraint_values = [
        "@platforms//os:macos",
        "@platforms//cpu:darwin_arm64",
    ],
)

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

# Reference to distributed BUILD targets
alias(
  name = "server",
  actual = "//sys/tests/e2e/server:e2e_server",
)

alias(
  name = "client", 
  actual = "//sys/tests/e2e/client:e2e_client_native",
)
