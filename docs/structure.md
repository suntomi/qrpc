cli --> rust project that provide commandline tool qrpcc
  Cargo.toml
bindings
  cpp --> probably reuses lib/src/qrpc
  c# --> highest priority
  rust
  go
  ts
  kotlin
  ...other languages...
tools
  scripts
    run_chrome_debug.sh
lib
  Cargo.toml
  BUILD
  tests
    e2e
      browser ---> selenium npm project
      client
        main.cpp
      server
        resources
          client.js ---> link from ../../../client/ts/client.js
        main.cpp
      ...test shell scripts...
      main.sh --> entry point of e2e tests
  ts ---> npm project
    dist --> build artifact of src
    src --> ts scripts
    package.json
    package-lock.json
    tsconfig.json
    README.md
  k8s ---> k8s operator/central server project to orchestrate qrpc server container fleet
    Cargo.toml
  ext
    moodycamel ---> keep
    mediasoup ---> keep
    libsdptransform ---> keep
    cares ---> will be provided from meson wrapdb after changing structure
    hedley ---> same as cares
    json ---> uses same version of mediasoup
    sha1 ---> keep
  base
    webrtc
  qrpc
  qrpc.h
  qrpc.cpp
Cargo.toml