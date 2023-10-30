# load("@//tools/bazel/libs:copy_file.bzl", "copy_file")

config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
)

config_setting(
    name = "darwin_x86_64",
    values = {"cpu": "darwin_x86_64"},
)

config_setting(
    name = "darwin_arm64",
    values = {"cpu": "darwin_arm64"},
)

config_setting(
    name = "darwin_arm64e",
    values = {"cpu": "darwin_arm64e"},
)

config_setting(
    name = "windows",
    values = {"cpu": "x64_windows"},
)

# Android is not officially supported through C++.
# This just helps with the build for now.
config_setting(
    name = "android",
    values = {
        "crosstool_top": "//external:android/crosstool",
    },
)

# iOS is not officially supported through C++.
# This just helps with the build for now.
config_setting(
    name = "ios_x86_64",
    values = {"cpu": "ios_x86_64"},
)

config_setting(
    name = "ios_armv7",
    values = {"cpu": "ios_armv7"},
)

config_setting(
    name = "ios_armv7s",
    values = {"cpu": "ios_armv7s"},
)

config_setting(
    name = "ios_arm64",
    values = {"cpu": "ios_arm64"},
)

config_setting(
    name = "ios_sim_arm64",
    values = {"cpu": "ios_sim_arm64"},
)

# The following architectures are found in 
# https://github.com/bazelbuild/bazel/blob/master/src/main/java/com/google/devtools/build/lib/rules/apple/ApplePlatform.java
config_setting(
    name = "tvos_x86_64",
    values = {"cpu": "tvos_x86_64"},
)

config_setting(
    name = "tvos_arm64",
    values = {"cpu": "tvos_arm64"}
)

config_setting(
    name = "watchos_i386",
    values = {"cpu": "watchos_i386"},
)

config_setting(
    name = "watchos_x86_64",
    values = {"cpu": "watchos_x86_64"}
)

config_setting(
    name = "watchos_armv7k",
    values = {"cpu": "watchos_armv7k"},
)

config_setting(
    name = "watchos_arm64_32",
    values = {"cpu": "watchos_arm64_32"}
)

config_setting(
    name = "openbsd",
    values = {"cpu": "openbsd"},
)

# copy_file(
#     name = "ares_build_h",
#     src = "@com_github_grpc_grpc//third_party/cares:ares_build.h",
#     out = "ares_build.h",
# )

# copy_file(
#     name = "ares_config_h",
#     src = select({
#         ":ios_x86_64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":ios_armv7": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":ios_armv7s": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":ios_arm64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":ios_sim_arm64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":tvos_x86_64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":tvos_arm64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":watchos_i386": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":watchos_x86_64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":watchos_armv7k": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":watchos_arm64_32": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":darwin": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":darwin_x86_64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":darwin_arm64": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":darwin_arm64e": "@com_github_grpc_grpc//third_party/cares:config_darwin/ares_config.h",
#         ":windows": "@com_github_grpc_grpc//third_party/cares:config_windows/ares_config.h",
#         ":android": "@com_github_grpc_grpc//third_party/cares:config_android/ares_config.h",
#         ":openbsd": "@com_github_grpc_grpc//third_party/cares:config_openbsd/ares_config.h",
#         "//conditions:default": "@com_github_grpc_grpc//third_party/cares:config_linux/ares_config.h",
#     }),
#     out = "ares_config.h",
# )

cc_library(
    name = "ares",
    srcs = [
        "cares/src/libares__read_line.c",
        "cares/src/libares__get_hostent.c",
        "cares/src/libares__close_sockets.c",
        "cares/src/libares__timeval.c",
        "cares/src/libares_gethostbyaddr.c",
        "cares/src/libares_getenv.c",
        "cares/src/libares_free_string.c",
        "cares/src/libares_free_hostent.c",
        "cares/src/libares_fds.c",
        "cares/src/libares_expand_string.c",
        "cares/src/libares_create_query.c",
        "cares/src/libares_cancel.c",
        "cares/src/libares_android.c",
        "cares/src/libares_parse_txt_reply.c",
        "cares/src/libares_parse_srv_reply.c",
        "cares/src/libares_parse_soa_reply.c",
        "cares/src/libares_parse_ptr_reply.c",
        "cares/src/libares_parse_ns_reply.c",
        "cares/src/libares_parse_naptr_reply.c",
        "cares/src/libares_parse_mx_reply.c",
        "cares/src/libares_parse_caa_reply.c",
        "cares/src/libares_options.c",
        "cares/src/libares_nowarn.c",
        "cares/src/libares_mkquery.c",
        "cares/src/libares_llist.c",
        "cares/src/libares_getsock.c",
        "cares/src/libares_getnameinfo.c",
        "cares/src/libbitncmp.c",
        "cares/src/libares_writev.c",
        "cares/src/libares_version.c",
        "cares/src/libares_timeout.c",
        "cares/src/libares_strerror.c",
        "cares/src/libares_strcasecmp.c",
        "cares/src/libares_search.c",
        "cares/src/libares_platform.c",
        "cares/src/libwindows_port.c",
        "cares/src/libinet_ntop.c",
        "cares/src/libares__sortaddrinfo.c",
        "cares/src/libares__readaddrinfo.c",
        "cares/src/libares_parse_uri_reply.c",
        "cares/src/libares__parse_into_addrinfo.c",
        "cares/src/libares_parse_a_reply.c",
        "cares/src/libares_parse_aaaa_reply.c",
        "cares/src/libares_library_init.c",
        "cares/src/libares_init.c",
        "cares/src/libares_gethostbyname.c",
        "cares/src/libares_getaddrinfo.c",
        "cares/src/libares_freeaddrinfo.c",
        "cares/src/libares_expand_name.c",
        "cares/src/libares_destroy.c",
        "cares/src/libares_data.c",
        "cares/src/libares__addrinfo_localhost.c",
        "cares/src/libares__addrinfo2hostent.c",
        "cares/src/libinet_net_pton.c",
        "cares/src/libares_strsplit.c",
        "cares/src/libares_strdup.c",
        "cares/src/libares_send.c",
        "cares/src/libares_rand.c",
        "cares/src/libares_query.c",
        "cares/src/libares_process.c",
    ],
    hdrs = [
        "ares_build.h",
        "cares/include/ares_version.h",
        "cares/include/ares.h",
        "cares/include/ares_rules.h",
        "cares/include/ares_dns.h",
        "cares/include/ares_nameser.h",
        "cares/src/tools/ares_getopt.h",
        "cares/src/libares_strsplit.h",
        "cares/src/libares_android.h",
        "cares/src/libares_private.h",
        "cares/src/libares_llist.h",
        "cares/src/libares_platform.h",
        "cares/src/libares_ipv6.h",
        "cares/src/libconfig-dos.h",
        "cares/src/libbitncmp.h",
        "cares/src/libares_strcasecmp.h",
        "cares/src/libsetup_once.h",
        "cares/src/libares_inet_net_pton.h",
        "cares/src/libares_data.h",
        "cares/src/libares_getenv.h",
        "cares/src/libconfig-win32.h",
        "cares/src/libares_strdup.h",
        "cares/src/libares_iphlpapi.h",
        "cares/src/libares_setup.h",
        "cares/src/libares_writev.h",
        "cares/src/libares_nowarn.h",        
    ] + select({
        ":ios_x86_64": ["config_darwin/ares_config.h"],
        ":ios_armv7": ["config_darwin/ares_config.h"],
        ":ios_armv7s": ["config_darwin/ares_config.h"],
        ":ios_arm64": ["config_darwin/ares_config.h"],
        ":ios_sim_arm64": ["config_darwin/ares_config.h"],
        ":tvos_x86_64": ["config_darwin/ares_config.h"],
        ":tvos_arm64": ["config_darwin/ares_config.h"],
        ":watchos_i386": ["config_darwin/ares_config.h"],
        ":watchos_x86_64": ["config_darwin/ares_config.h"],
        ":watchos_armv7k": ["config_darwin/ares_config.h"],
        ":watchos_arm64_32": ["config_darwin/ares_config.h"],
        ":darwin": ["config_darwin/ares_config.h"],
        ":darwin_x86_64": ["config_darwin/ares_config.h"],
        ":darwin_arm64": ["config_darwin/ares_config.h"],
        ":darwin_arm64e": ["config_darwin/ares_config.h"],
        ":windows": ["config_windows/ares_config.h"],
        ":android": ["config_android/ares_config.h"],
        ":openbsd": ["config_openbsd/ares_config.h"],
        "//conditions:default": ["config_linux/ares_config.h"],
    }),
    copts = [
        "-D_GNU_SOURCE",
        "-D_HAS_EXCEPTIONS=0",
        "-DHAVE_CONFIG_H",
    ] + select({
        ":windows": [
            "-DNOMINMAX",
            "-D_CRT_SECURE_NO_DEPRECATE",
            "-D_CRT_NONSTDC_NO_DEPRECATE",
            "-D_WIN32_WINNT=0x0600",
        ],
        "//conditions:default": [],
    }),
    defines = ["CARES_STATICLIB"],
    includes = ["include", "."],
    linkopts = select({
        ":windows": ["-defaultlib:ws2_32.lib"],
        "//conditions:default": [],
    }),
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
    alwayslink = 1,
)
