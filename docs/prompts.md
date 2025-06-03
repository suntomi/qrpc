at 2021b8ec62f5f09fe212a6d434e11916da078cbb
現在bazelを使ってビルドされるのは
sys/server, sys/tests/e2e/client/native, sys/tests/e2e/server のファイルです。
現在のルートディレクトリのBUILDを見るとわかるように、後ろの２つのファイルはsys/serverのビルドに依存しています。
この事実を元にあなたの新しい提案のような形でBUILDファイルを分散して配置して、今までのようにトップレベルのmakefileからビルドを行えるように変更を行なってください。

you completely remove mediasoup target, should you put it root BUILD file again?

build system works as expect but have some compile error. I think error happens because -D option that sys/server/BUILD provides are missing for e2e_server and e2e_client_native targets. can you fix this?

results -> c2d066e6dda4f231b29815fc2b1f9560eec86b7f


at 8352639891758ab5dd9d294847ebc8da220822d2
sys/client/ts/client.jsをtsに変換したいです。npmパッケージとしてビルド、パブリッシュできるように環境整備も行なってください。

results -> bf182ac335f7f420370c552e24a9443e1c710cce

at bf182ac335f7f420370c552e24a9443e1c710cce
current sys/client/ts genrates multiple js files as compile result. but I want to generate concatenate version of these files because generated js file will be downloaded from browser. concatenate & minify files improve startup time of pages that just use sys/client/ts as javascript library.

results -> 7646e72e09775644e33b14d639262e4a40f330e0

at 7646e72e09775644e33b14d639262e4a40f330e0
I made some fix and now it works. then I have question.
esbuild itself does not generate .d.ts? if so, should I change configuration to generate .d.ts file?

* need to fix manually to remove compile error (remove tsconfig.types.json)

now I could generate bundled js and .d.ts files. individual js need to be generated?

results -> 2f4c1fac458cc741a3e22b4c81c698e95d83e429

at 2f4c1fac458cc741a3e22b4c81c698e95d83e429
nice. next I want to watch typescript codebase and update qrpc.bundle.js when typescript code changes.

can you use english for message in esbuild.config.js?

result -> 2303f85b179f6694624c0f83bee3ca9831f4ed1e