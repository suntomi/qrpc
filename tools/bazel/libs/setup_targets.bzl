def setup_targets():
  native.config_setting(
      name = "darwin",
      values = {"cpu": "darwin"},
  )

  native.config_setting(
      name = "darwin_x86_64",
      values = {"cpu": "darwin_x86_64"},
  )

  native.config_setting(
      name = "darwin_arm64",
      values = {"cpu": "darwin_arm64"},
  )

  native.config_setting(
      name = "darwin_arm64e",
      values = {"cpu": "darwin_arm64e"},
  )

  native.config_setting(
      name = "windows",
      values = {"cpu": "x64_windows"},
  )

  # Android is not officially supported through C++.
  # This just helps with the build for now.
  native.config_setting(
      name = "android",
      values = {
          "crosstool_top": "//external:android/crosstool",
      },
  )

  # iOS is not officially supported through C++.
  # This just helps with the build for now.
  native.config_setting(
      name = "ios_x86_64",
      values = {"cpu": "ios_x86_64"},
  )

  native.config_setting(
      name = "ios_armv7",
      values = {"cpu": "ios_armv7"},
  )

  native.config_setting(
      name = "ios_armv7s",
      values = {"cpu": "ios_armv7s"},
  )

  native.config_setting(
      name = "ios_arm64",
      values = {"cpu": "ios_arm64"},
  )

  native.config_setting(
      name = "ios_sim_arm64",
      values = {"cpu": "ios_sim_arm64"},
  )

  # The following architectures are found in 
  # https://github.com/bazelbuild/bazel/blob/master/src/main/java/com/google/devtools/build/lib/rules/apple/ApplePlatform.java
  native.config_setting(
      name = "tvos_x86_64",
      values = {"cpu": "tvos_x86_64"},
  )

  native.config_setting(
      name = "tvos_arm64",
      values = {"cpu": "tvos_arm64"}
  )

  native.config_setting(
      name = "watchos_i386",
      values = {"cpu": "watchos_i386"},
  )

  native.config_setting(
      name = "watchos_x86_64",
      values = {"cpu": "watchos_x86_64"}
  )

  native.config_setting(
      name = "watchos_armv7k",
      values = {"cpu": "watchos_armv7k"},
  )

  native.config_setting(
      name = "watchos_arm64_32",
      values = {"cpu": "watchos_arm64_32"}
  )

  native.config_setting(
      name = "openbsd",
      values = {"cpu": "openbsd"},
  )

  # QRPC Linux platform support
  native.config_setting(
      name = "linux_arm64",
      values = {"cpu": "linux_arm64"},
  )

  native.config_setting(
      name = "linux_amd64",
      values = {"cpu": "linux_amd64"},
  )