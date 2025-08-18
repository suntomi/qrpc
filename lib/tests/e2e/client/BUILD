load("//tools/bazel/libs:selects.bzl", "selects")
load("//tools/bazel/libs:ms_cppargs.bzl", "MS_CPPARGS")

cc_binary(
  name = "e2e_client_native",
  srcs = ["native/main.cpp"],
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
      "-D__ENABLE_EPOLL__"
    ],
  }),
  linkopts = selects.with_or({
    "//:asan": ["-fsanitize=address"],
    "//conditions:default": [],
  }),
  deps = ["//lib:qrpc_server_lib"],
  linkstatic = True,
  visibility = ["//visibility:public"],
)
