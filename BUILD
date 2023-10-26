load("//bazel/defs:variables.bzl", "MTK_PLATFORMS", "make_platform")

[make_platform(platform, MTK_PLATFORMS[platform]) for platform in MTK_PLATFORMS]
    
cc_binary(
    name = "server",
    includes = glob(["src/*.h"])
    srcs = glob(["src/*.cpp", "test/lab/webrtc/server/*.cpp"]),
    deps = ["//:libwebrtc_osx_arm64", "//test/lab/webrtc/ext/cares:ares"]
)