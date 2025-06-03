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

at 7646e72e09775644e33b14d639262e4a40f330e0
I made some fix and now it works. then I have question.
esbuild itself does not generate .d.ts? if so, should I change configuration to generate .d.ts file?

