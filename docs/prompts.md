at 2021b8ec62f5f09fe212a6d434e11916da078cbb
現在bazelを使ってビルドされるのは
sys/server, sys/tests/e2e/client/native, sys/tests/e2e/server のファイルです。
現在のルートディレクトリのBUILDを見るとわかるように、後ろの２つのファイルはsys/serverのビルドに依存しています。
この事実を元にあなたの新しい提案のような形でBUILDファイルを分散して配置して、今までのようにトップレベルのmakefileからビルドを行えるように変更を行なってください。

you completely remove mediasoup target, should you put it root BUILD file again?

build system works as expect but have some compile error. I think error happens because -D option that sys/server/BUILD provides are missing for e2e_server and e2e_client_native targets. can you fix this?

results -> c2d066e6dda4f231b29815fc2b1f9560eec86b7f

