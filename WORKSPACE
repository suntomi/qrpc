workspace(name = "qrpc")

# load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# http_archive(
#     name = "rules_foreign_cc",
#     strip_prefix = "rules_foreign_cc-3a85c822bf8bd44ca427c27407e838fdecd6bc86",
#     url = "https://github.com/bazelbuild/rules_foreign_cc/archive/3a85c822bf8bd44ca427c27407e838fdecd6bc86.tar.gz",
# )

# load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

# rules_foreign_cc_dependencies()

config_setting(
    name = "is_debug_build",
    values = {
        "compilation_mode": "dbg",
    },
    visibility = ["//:__pkg__"],
)