at 2021b8ec62f5f09fe212a6d434e11916da078cbb
現在bazelを使ってビルドされるのは
sys/server, sys/tests/e2e/client/native, sys/tests/e2e/server のファイルです。
現在のルートディレクトリのBUILDを見るとわかるように、後ろの２つのファイルはsys/serverのビルドに依存しています。
この事実を元にあなたの新しい提案のような形でBUILDファイルを分散して配置して、今までのようにトップレベルのmakefileからビルドを行えるように変更を行なってください。

you completely remove mediasoup target, should you put it root BUILD file again?

build system works as expect but have some compile error. I think error happens because -D option that sys/server/BUILD provides are missing for e2e_server and e2e_client_native targets. can you fix this?

results -> c2d066e6dda4f231b29815fc2b1f9560eec86b7f

at 263f2588bb4035f138b019f241eb5222926638cf

現在bazelでビルドしたバイナリファイルをlldbでデバッグすると、コールスタックにファイル名や行数が表示されません。
表示されるようにして欲しいです。以前同じ問題があり、その時は --features=oso_prefix_is_pwd オプションをbazel buildに追加することで問題が解決しました。

修正されていません。新しいe2e_client_nativeをlldbで動かしてbtしましたが、以下のようにファイル名や行数は表示されていません。
```
(lldb) bt
* thread #1, queue = 'com.apple.main-thread', stop reason = hit program assert
    frame #0: 0x0000000187980704 libsystem_kernel.dylib`__pthread_kill + 8
    frame #1: 0x00000001879b7c28 libsystem_pthread.dylib`pthread_kill + 288
    frame #2: 0x00000001878c5ae8 libsystem_c.dylib`abort + 180
    frame #3: 0x00000001878c4e44 libsystem_c.dylib`__assert_rtn + 272
  * frame #4: 0x00000001000e9590 e2e_client_native`base::UdpClient::Open(base::Address const&, std::__1::function<base::SessionFactory::Session* (int, base::Address const&)>) + 1024
    frame #5: 0x00000001003f312c e2e_client_native`base::SessionDnsQuery::OnComplete(int, int, hostent*) + 1040
    frame #6: 0x000000010022b818 e2e_client_native`base::Resolver::Query::OnComplete(void*, int, int, hostent*) + 692
```

こちらで修正できました。bazelの出力ディレクトリ(bazel-out)を.buildの下に移動したことが原因でした。oso_prefix_is_pwdをoso_prefix=.buildに変更することでファイル名と行数が正しく表示されるようになりました。

