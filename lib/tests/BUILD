filegroup(
  name = "e2e",
  srcs = [
    "//lib/tests:core",
    "//lib/tests:qrpc",
  ],
  visibility = ["//visibility:public"],
)

filegroup(
  name = "core",
  srcs = [
    "//lib/tests/e2e/core/server:e2e_server",
    "//lib/tests/e2e/core/client:e2e_client_native",
  ]
)

filegroup(
  name = "qrpc",
  srcs = [
    "//lib/tests/e2e/qrpc/server:e2e_qrpc_server",
  ],
)
