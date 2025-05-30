cli --> rust project that provide commandline tool qrpcc
  Cargo.toml
bindings
  cpp --> probably reuses sys/server/src/qrpc
  c# --> highest priority
  rust
  go
  ts
  kotlin
  ...other languages...
tools
  scripts
    run_chrome_debug.sh
sys
  Cargo.toml
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
  client 
    ts ---> npm project
      client.ts
      client.js --> built from client.ts
  k8s ---> k8s operator/central server project to make qrpc server container fleet
    Cargo.toml
  server ---> build with meson.build invoked from Cargo.toml
    ext
      moodycamel ---> keep
      mediasoup ---> keep
      libsdptransform ---> keep
      cares ---> will be provided from meson wrapdb after changing structure
      hedley ---> same as cares
      json ---> uses same version of mediasoup
      sha1 ---> keep
    src
      base
        webrtc
      qrpc
    include
      qrpc.h
    Cargo.toml