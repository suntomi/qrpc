load("@//tools/bazel/libs:copy_file.bzl", "copy_file")

copy_file(
    name = "ares_config_h",
    src = select({
        "//:ios_x86_64": "config_darwin/ares_config.h",
        "//:ios_armv7": "config_darwin/ares_config.h",
        "//:ios_armv7s": "config_darwin/ares_config.h",
        "//:ios_arm64": "config_darwin/ares_config.h",
        "//:ios_sim_arm64": "config_darwin/ares_config.h",
        "//:tvos_x86_64": "config_darwin/ares_config.h",
        "//:tvos_arm64": "config_darwin/ares_config.h",
        "//:watchos_i386": "config_darwin/ares_config.h",
        "//:watchos_x86_64": "config_darwin/ares_config.h",
        "//:watchos_armv7k": "config_darwin/ares_config.h",
        "//:watchos_arm64_32": "config_darwin/ares_config.h",
        "//:darwin": "config_darwin/ares_config.h",
        "//:darwin_x86_64": "config_darwin/ares_config.h",
        "//:darwin_arm64": "config_darwin/ares_config.h",
        "//:darwin_arm64e": "config_darwin/ares_config.h",
        "//:windows": "config_windows/ares_config.h",
        "//:android": "config_android/ares_config.h",
        "//:openbsd": "config_openbsd/ares_config.h",
        "//conditions:default": "config_linux/ares_config.h",
    }),
    out = "ares_config.h",
)

cc_library(
    name = "ares",
    srcs = [
        "cares/src/lib/ares__read_line.c",
        "cares/src/lib/ares__get_hostent.c",
        "cares/src/lib/ares__close_sockets.c",
        "cares/src/lib/ares__timeval.c",
        "cares/src/lib/ares_gethostbyaddr.c",
        "cares/src/lib/ares_getenv.c",
        "cares/src/lib/ares_free_string.c",
        "cares/src/lib/ares_free_hostent.c",
        "cares/src/lib/ares_fds.c",
        "cares/src/lib/ares_expand_string.c",
        "cares/src/lib/ares_create_query.c",
        "cares/src/lib/ares_cancel.c",
        "cares/src/lib/ares_android.c",
        "cares/src/lib/ares_parse_txt_reply.c",
        "cares/src/lib/ares_parse_srv_reply.c",
        "cares/src/lib/ares_parse_soa_reply.c",
        "cares/src/lib/ares_parse_ptr_reply.c",
        "cares/src/lib/ares_parse_ns_reply.c",
        "cares/src/lib/ares_parse_naptr_reply.c",
        "cares/src/lib/ares_parse_mx_reply.c",
        "cares/src/lib/ares_parse_caa_reply.c",
        "cares/src/lib/ares_options.c",
        "cares/src/lib/ares_nowarn.c",
        "cares/src/lib/ares_mkquery.c",
        "cares/src/lib/ares_llist.c",
        "cares/src/lib/ares_getsock.c",
        "cares/src/lib/ares_getnameinfo.c",
        "cares/src/lib/bitncmp.c",
        "cares/src/lib/ares_writev.c",
        "cares/src/lib/ares_version.c",
        "cares/src/lib/ares_timeout.c",
        "cares/src/lib/ares_strerror.c",
        "cares/src/lib/ares_strcasecmp.c",
        "cares/src/lib/ares_search.c",
        "cares/src/lib/ares_platform.c",
        "cares/src/lib/windows_port.c",
        "cares/src/lib/inet_ntop.c",
        "cares/src/lib/ares__sortaddrinfo.c",
        "cares/src/lib/ares__readaddrinfo.c",
        "cares/src/lib/ares_parse_uri_reply.c",
        "cares/src/lib/ares__parse_into_addrinfo.c",
        "cares/src/lib/ares_parse_a_reply.c",
        "cares/src/lib/ares_parse_aaaa_reply.c",
        "cares/src/lib/ares_library_init.c",
        "cares/src/lib/ares_init.c",
        "cares/src/lib/ares_gethostbyname.c",
        "cares/src/lib/ares_getaddrinfo.c",
        "cares/src/lib/ares_freeaddrinfo.c",
        "cares/src/lib/ares_expand_name.c",
        "cares/src/lib/ares_destroy.c",
        "cares/src/lib/ares_data.c",
        "cares/src/lib/ares__addrinfo_localhost.c",
        "cares/src/lib/ares__addrinfo2hostent.c",
        "cares/src/lib/inet_net_pton.c",
        "cares/src/lib/ares_strsplit.c",
        "cares/src/lib/ares_strdup.c",
        "cares/src/lib/ares_send.c",
        "cares/src/lib/ares_rand.c",
        "cares/src/lib/ares_query.c",
        "cares/src/lib/ares_process.c",
    ],
    hdrs = [
        "ares_build.h",
        "ares_config.h",
        "cares/include/ares_version.h",
        "cares/include/ares.h",
        "cares/include/ares_rules.h",
        "cares/include/ares_dns.h",
        "cares/include/ares_nameser.h",
        "cares/src/tools/ares_getopt.h",
        "cares/src/lib/ares_strsplit.h",
        "cares/src/lib/ares_android.h",
        "cares/src/lib/ares_private.h",
        "cares/src/lib/ares_llist.h",
        "cares/src/lib/ares_platform.h",
        "cares/src/lib/ares_ipv6.h",
        "cares/src/lib/config-dos.h",
        "cares/src/lib/bitncmp.h",
        "cares/src/lib/ares_strcasecmp.h",
        "cares/src/lib/setup_once.h",
        "cares/src/lib/ares_inet_net_pton.h",
        "cares/src/lib/ares_data.h",
        "cares/src/lib/ares_getenv.h",
        "cares/src/lib/config-win32.h",
        "cares/src/lib/ares_strdup.h",
        "cares/src/lib/ares_iphlpapi.h",
        "cares/src/lib/ares_setup.h",
        "cares/src/lib/ares_writev.h",
        "cares/src/lib/ares_nowarn.h",    
    ],
    copts = [
        "-D_GNU_SOURCE",
        "-D_HAS_EXCEPTIONS=0",
        "-DHAVE_CONFIG_H",
    ] + select({
        "//:windows": [
            "-DNOMINMAX",
            "-D_CRT_SECURE_NO_DEPRECATE",
            "-D_CRT_NONSTDC_NO_DEPRECATE",
            "-D_WIN32_WINNT=0x0600",
        ],
        "//conditions:default": [],
    }),
    defines = ["CARES_STATICLIB"],
    includes = ["cares/include", "."],
    linkopts = select({
        "//:windows": ["-defaultlib:ws2_32.lib"],
        "//conditions:default": [],
    }),
    linkstatic = 1,
    visibility = [
        "//visibility:public",
    ],
    alwayslink = 1,
)
