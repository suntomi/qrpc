============================
at 2021b8ec62f5f09fe212a6d434e11916da078cbb
現在bazelを使ってビルドされるのは
lib, sys/tests/e2e/client/native, sys/tests/e2e/server のファイルです。
現在のルートディレクトリのBUILDを見るとわかるように、後ろの２つのファイルはlibのビルドに依存しています。
この事実を元にあなたの新しい提案のような形でBUILDファイルを分散して配置して、今までのようにトップレベルのmakefileからビルドを行えるように変更を行なってください。

you completely remove mediasoup target, should you put it root BUILD file again?

build system works as expect but have some compile error. I think error happens because -D option that lib/BUILD provides are missing for e2e_server and e2e_client_native targets. can you fix this?

results -> c2d066e6dda4f231b29815fc2b1f9560eec86b7f

============================
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

============================
at 8352639891758ab5dd9d294847ebc8da220822d2
sys/client/ts/client.jsをtsに変換したいです。npmパッケージとしてビルド、パブリッシュできるように環境整備も行なってください。

results -> bf182ac335f7f420370c552e24a9443e1c710cce
============================
at bf182ac335f7f420370c552e24a9443e1c710cce
current sys/client/ts genrates multiple js files as compile result. but I want to generate concatenate version of these files because generated js file will be downloaded from browser. concatenate & minify files improve startup time of pages that just use sys/client/ts as javascript library.

results -> 7646e72e09775644e33b14d639262e4a40f330e0
============================
at 7646e72e09775644e33b14d639262e4a40f330e0
I made some fix and now it works. then I have question.
esbuild itself does not generate .d.ts? if so, should I change configuration to generate .d.ts file?

* need to fix manually to remove compile error (remove tsconfig.types.json)

now I could generate bundled js and .d.ts files. individual js need to be generated?

results -> 2f4c1fac458cc741a3e22b4c81c698e95d83e429
============================
at 2f4c1fac458cc741a3e22b4c81c698e95d83e429
nice. next I want to watch typescript codebase and update qrpc.bundle.js when typescript code changes.

can you use english for message in esbuild.config.js?

result -> 2303f85b179f6694624c0f83bee3ca9831f4ed1e

============================
at 2cdadee87056b1f9383d82dce37e7f679c49dd0f
replace compiler specific pragma with HEDLEY macro

result -> 5b6aff2bc119ed9c356225c4b4538dd62961e640

============================
at 5b6aff2bc119ed9c356225c4b4538dd62961e640

コンパイルは通りました。macro.hにマクロはまとめたいので、diagnostic_macros.h の内容をmacro.hにマージしてください

result -> 1691d6e170c67b4d545950558c7e729533884fe0

============================
at b4c719a43c1ee8ac03136b693220a0cc874cfbb1

step1
linuxコンテナでsys/test/e2e/serverでビルドされるバイナリが動作するようにします。
- コンテナイメージを作成するためのDockerfile
- コンテナを動かす環境としてkubernetesを使いたいので、まずローカル環境のための設定ファイル
を作成して欲しいです。

まずどこにどういうファイルを作成するかの計画を教えてください


step2
作られたファイルについていくつか教えてください
- マルチステージビルドのためのdockerfileですが、２つに分かれています。私の理解ではマルチステージビルドを行う場合には１つのdockerfileに複数のステージを記述する必要があったと思うのですが、最近のdockerではその必要がなくなったのでしょうか？

============================
<functional> header included from nlohmann/json.hpp causes similar warning ```#10 36.93 In file included from /usr/include/c++/13/functional:59,
#10 36.93                  from lib/ext/libsdptransform/include/json.hpp:23,
#10 36.93                  from lib/ext/libsdptransform/include/sdptransform.hpp:4,
#10 36.93                  from lib/ext/libsdptransform/src/parser.cpp:1:
#10 36.93 In constructor 'std::function<_Res(_ArgTypes ...)>::function(std::function<_Res(_ArgTypes ...)>&&) [with _Res = bool; _ArgTypes = {char}]',
#10 36.93     inlined from 'std::__detail::_State<_Char_type>::_State(std::__detail::_State<_Char_type>&&) [with _Char_type = char]' at /usr/include/c++/13/bits/regex_automaton.h:149:4,
#10 36.93     inlined from 'std::__detail::_State<_Char_type>::_State(std::__detail::_State<_Char_type>&&) [with _Char_type = char]' at /usr/include/c++/13/bits/regex_automaton.h:146:7,
#10 36.93     inlined from 'std::__detail::_StateIdT std::__detail::_NFA<_TraitsT>::_M_insert_subexpr_end() [with _TraitsT = std::__cxx11::regex_traits<char>]' at /usr/include/c++/13/bits/regex_automaton.h:290:24:
#10 36.93 /usr/include/c++/13/bits/std_function.h:405:42: warning: '*(std::function<bool(char)>*)((char*)&__tmp + offsetof(std::__detail::_StateT, std::__detail::_State<char>::<unnamed>.std::__detail::_State_base::<unnamed>)).std::function<bool(char)>::_M_invoker' may be used uninitialized [-Wmaybe-uninitialized]```

fix it by following storategy:

1. create wrapper header of json.hpp at lib/base/wrapped/json.hpp
2. wrapped nlohmann/json.hpp with DISABLE_MAYBE_UNINITIALIZED_WARNING_PUSH/POP
3. replace #include <nlohmann/json.hpp> to #include "base/wrapped/json.hpp"


===================
linux版のe2e_serverをdebug mode + address sanitizerを有効にしてビルドすると、
docs/reference/asan/schedule_close.txt
のようなエラーを報告します。このバグを修正してください。
動作確認はこちらで行いますので、修正が完了したら入力を待ってください。


===================
このレポジトリでは、ストリームベースの多重化された通信を行い、さらにwebrtcのSFUとしても動作するqrpcというCライブラリを開発しています。
ヘッダーはlib/qrpc.hにあります。現在このライブラリはブラウザクライアント lib/web/ts/src/*.ts とは正しくwebrtcで通信ができています。
このライブラリに以下のような機能を設けたいです。
- クライアント機能をCライブラリ側でも利用できるようにしたい
- サーバーはクライアントから送られてくるmedia streamをコールバックで受け取れるようにしたい

このためにqrpc_media_**** というインターフェイスを用意しようとしていますが、ベースはqrpc_conn_tに対して呼び出しを行うようにしたいです。
qrpc_conn_tはサーバー/クライアントの接続を共通で表すハンドルです。

ですが、クライアントが以下のように多様な機能を必要とするのに比較して、サーバー側はコールバックの設定しかありません。
つまり、同じqrpc_conn_tでも行える操作に非対称性があります。この非対称性をうまく表現できるようなインターフェイスにしたいと考えています。
この場合、qrpc_media_***はどのようなインターフェイスにするのが良いと考えますか？

===================
lib/base/webrtc/sdp.cpp に SDP::CapSdpTextFromを実装してください。この関数はlib/qrpc.hに定義されているqrpc_media_config_tを受け取って、この構造体が表しているaudio/video sectionのSDPを生成します。

===================
このレポジトリではmediasoupをベースとしたRTP/RTCPの処理を実装しています。サーバー(SFU)側の実装には特に問題はないです。
クライアントを実装する場合、rtp packetを生成して接続先に送信する必要がありますが、mediasoupは基本的にSFUを実装しているため送信側の処理がありません。
そのため、以下のような手段を取ろうと考えています。
1. rtp::Handlerにproducerを生成する。これが送信されるrtp packetを扱う窓口となる。
2. このproducerを自分自身(=base::webrtc::Client::Connection)でconsumeする。
3. 送信したいrtp packetを、midやridを調整して1.で作られたproducerが処理するようにローカルで生成する。
4. そのバイト列表現をHandler::ReceiveRtpPacketに渡す。
5. 対応するrtp packetはproducerで処理され、consumerに渡される。consumerは自分自身のpeerにrtp packetを送信するはず。

この実装で問題なく動作しそうかmediasoupのコードを確認してください。特に懸念しているのは、rtp packetを送信する場合にpeerからの要求に従って適宜rtcp packetを送る必要があると思いますが、mediasoupのproducerがそれを全て処理できるか、こちら側で何かしらの追加実装を行う必要があるかについて確認したいです。

=====
ありがとうございます。FIR/PILの対応を行うための実装は以下のように行おうと考えています。
ConnectionFactory::Connection::SendRtcpPacket

=====
lib/tests/e2e/qrpc/client/main.cpp に lib/qrpc.h のAPIを使ったクライアントを作成してください。
lib/qrpc.hのAPIが行う通信は


.build/bazel-bin/lib/tests/e2e/core/client/e2e_client_native を動かした時に発生する以下のaddress sanitizerエラーを修正してください。
e2e_client_native は lib/tests/e2e/core/client/native/main.cpp をbazelでビルドして生成され、BUILDファイルは lib/tests/e2e/core/client/BUILD です。

======

lib/tests/e2e/core/client/native/main.cpp のsdpのテキストは長く、ソースコード中に存在すると可読性に影響があるので別ファイルにしてください


=======
lib/base/session_base.h の SessionFactory::Connect がsslを使うかどうかは現在、SessionFactoryを作るときに渡されたConfigのMaybeCertPairの値で決定されます。
しかし、理想的には、SessionFactory::Connect がSSLを必要とするかどうかはConnectごとに決定されるべきです。

この問題を解決し、base::SessionFactoryのクライアントとサーバーとしての責務をより明確に分割するためにはどのような修正を行うのが良いでしょうか？プランを教えてください。
- SessionFactoryから、TLS関連の処理やConnect()をなくす, SessionFactory::Configからsession timeout以外の設定をなくす
- SessionFactory::SessionからClose()の再接続をなくす
- 新たにClientSessionFactory, ListenerSessionFactoryをSessionFactoryを継承する形で作成する(session_base.hに実装)
  - ClientSessionFactory::SessionはMaybeCertPairを持つが、ClientSessionFactory自身はtls_ctx_を持たない。現在のSessionFactory::Openもこちらのクラスのみが持つようにする
    - SessionFactory::Session::Closeの再接続処理もClientSessionFactory::Closeに移動する
    - SessionFactory::Config::resolverをClientSessionFactory::Config::resolverに移動
  - ListenerSessionFactory::SessionはMaybeCertPairを持たない(SessionFactory::Sessionをそのまま使う)が、tls_ctx_とその初期化処理を持つ
    - SessionFactory::Config::certpairはListenerSessionFactory::Configに移動
- TcpClient, UdpClientはClientSessionFactory、TcpListener, UdpListenerはListenerSessionFactoryを継承する
- ClientSessionFactory::Connect にはMaybeCertPairを渡せるようにする

1. HttpClientのコンストラクタにcertpairを渡していますが、HttpClientではConnect()にcertpairを渡せるようにする必要があります。

=======

lib/base/session_base.h の SessionFactory::Connect は本来base::TcpClientやbase::UdpClientといったclient系のオブジェクトでしか使うことができません。
この責務の分離をコード上でよりよく表現するために修正を行います。

最初のステップは
- 新たにClientSessionFactory, ListenerSessionFactoryをSessionFactoryを継承する形で作成する(session_base.hに実装)
- TcpClient, UdpClientはClientSessionFactory、TcpListener, UdpListenerはListenerSessionFactoryを継承する
です。

SessionFactory::Connectはlistener系の派生クラス(TcpListener/UdpListener)では呼ぶことができない方が良いと考えます。
そこで新たにClientSessionFactory, ListenerSessionFactoryをSessionFactoryを継承する形で作成し、SessionFactory::ConnectをClientSessionFactoryに移動させる修正を行いたいです。

その場合はどのように実装しますか？

TcpSessionFactory, UdpSessionFactoryを、継承するクラスを型引数として受け取るテンプレートクラスにして、ListenerSessionFactory, ClientSessionFactoryを与えることでListener/Client用のTcpSessionFactory, UdpSessionFactoryを生成させるのはどうですか

TransportModeは、tlsが有効かどうか、とサーバーかクライアントか、という異なる属性が１つのenumで管理されているのが良くないと考えます。
SessionFactory::Sessionに仮想関数is_listener()とneed_tls()を用意し、これらが
- ListenerSessionFactoryで作られたSessionか、ClientSessionFactoryで作られたSessionか
  - これはそれぞれのSessionFactoryでSessionFactory::Sessionを継承し、適切にoverrideすれば良い
- tlsが有効か無効か
  - ListenerSessionFactoryではConfigに有効なCertificatePair渡されたかどうか
  - ClientSessionFactoryではConnectOptionでuse_tlsが指定されたかどうか
を返せば良いのではないでしょうか？

ConnectごとのConnectOptionsをsessionに渡す方法としては、FactoryMethodをラップするようにします。この前提で進めてください。

ここまでの修正プランを docs/plans/per_connect_ssl_options.md にまとめてください。

- ConnectOptionはConnectを持っているClientSessionFactoryが持っていれば良いのではないでしょうか
- SessionFactory::Connectが残っています。ClientSessionFactoryにConnectを移動させてください
- ResolverがSessionFactoryに残っています。デザインドキュメントの通り、ClientSessionFactory::Configに移動させてください

- Connectに関係がないので、ConnectOptionsはSessionFactory::Sessionには不要ではないでしょうか。
- SessionFactory::ConfigにcertpairやResolverが存在しています。これらはSessionFactory::Configには不要に見えます
  - ResolverはClientSessionFactory::Configにあれば良いはずです
  - certpairはListenerSessionFactory::Configにあれば良いはずです。

Session::ApplyConnectOptionsはClientSessionFactory::Sessionに移動できると思います。そうすれば、Sessionにclient_tls_enabled_を持つ必要はなく、この変数は
おそらくClientSessionFactory::Session::tls_enabled_のような変数に移動できます。

http.hのHttpSessionですが、TcpSessionがTcpClientSessionとTcpListenerSessionに分かれたので、lib/base/session.hのTcpSessionTのような方針で、HttpSessionTを実装して継承元をTcpClientSessionとTcpListenerSessionのどちらでも指定できるようにし、HttpClientSessionとHttpListenerSessionを定義できるようにします。

- HttpProtocolクラスを定義して、今のHttpSessionのOnRead/Sendとfsm_へのアクセサ以外のメンバ関数や型定義はそれに移動させる
- HttpProtocolクラスにProcessRead(HttpFSM &fsm, const char *p, size_t sz)を実装し、今のHttpSession::OnReadを移設する
- HttpSessionTはテンプレートパラメーターとして与えられたクラスと別にHttpProtocolクラスをmixinとして継承する
- HttpSession::OnReadはHttpProtocol::ProcessRead(fsm_, p, sz)を呼ぶだけにする

ProcessReadが要求するSessionの機能を全部見積もれていなかったため、仮想関数が大量に作られてしまいました。
これは無駄に複雑なため、以下のようにしたいと思います。
- virtual int HttpProtocol::OnFinishRead()を作成し、http.cpp:529-544の処理を return OnFinishRead()で置き換える。
- HttpWritevはどのような時に呼ばれるか、を明確に表していないため、OnSendPayloadという名前にします。

次にWebSocket側も同様に修正を行います。

まずWebSocketProtocolクラスを作成し、WebSocketSession::State, opcode, Frameの定義を移動させてWebSocketSessionにmixinするようにしてください。

現在のWebSocketSessionはHttpFSMにあたる部分がSessionの実装に混じってしまっていて見通しが悪いため、WebSocketFSMというクラスを作成し以下の部分を移動させます。

- 状態を保持している全てのメンバ変数(m_ではじまっている)
- WebSocketSessionの実装のうち、read_frameとhandshake、およびこれらの関数から呼び出されている全ての関数

そしてWebSocketSessionはWebSocketFSMをメンバ変数として持つようにし、handshakeとread_frameはWebSocketFSMのものを呼ぶようにしてください。

- 作成されたコードを見るとHttpFSMはWebSocketFSMに含まれていてよさそうですので、移動させてください
- WebSocketFSM::ControlFrameはWebSocketProtocolにあるべき定義なので、移動させてください。
- WebSocketFSMの読み込み関数やWebSocketFSM::ControlFrame::drainにWebSocketSessionを渡していますが、WebSocketSessionは後でWebSocketSessionTとなるため、利用できません。呼び出している関数を見るとSessionで十分ですので変更してください。

ではWebSocketSessionをWebSocketSessionTとして、TcpClientSession/TcpListenerSessionの両方を継承クラスとして受け取れるようにしてください。今WebSocketSessionはTcpSessionFactoryを受け取っていますが、これは以下のようにして、正しいFactoryを受け取れるようにします。

1. ClientSessionFactory::SessionとListenerSessionFactory::SessionにFactoryという型定義を追加する。
  - ClientSessionFactory::Session では、typedef ClientSessionFactory Factory;
  - ListenerSessionFactory::Session では、typedef ListenerSessionFactory Factory;
2. WebSocketSessionFactoryでは、以下のような定義をする
  - typedef TcpSessionFactory<typename SessionBase::Factory> Factory;
3. WebSocketSessionのコンストラクタでは上のFactoryを受け取るようにする


=======

lib/tests/e2e/core/run.sh に lib/tests/e2e/core/suites 以下のテストスイートを実行するテストランナーを作成します。
以下のような手順で動作します。

1. .build/bazel-bin/lib/tests/e2e/core/server/e2e_server　を lib/tests/tools/debugger.sh の with_dbgで起動する。
2. この際.  .build/bazel-bin/lib/tests/e2e/core/server/e2e_server の pidを調べて記憶しておき、trapでrun.shが終了する際にはシャットダウンするようにする(kill -TERM $PID)
3. suites以下のスクリプトを全部実行する。0以外でexitした場合にも処理を打ち切らず、最後に失敗したスクリプト名のリストを表示する

=======

原則として native clientは lib/web/ts/src/client.ts に準拠した動作をすべきなので、cnameだけではなく、capabilityも送る必要があります。いくつか仕様上決めるべき点があると思うので列挙します。

- cname, capabilityを保持する場所
  - SDP::CapSdpFrom(qrpc_media_config_t &)があるので、capabilityはqrpc_media_config_tの形で持っておけば良さそう
  - cnameもqrpc_media_config_tに追加する
- webrtc::Client::Configはwebrtc::ConnectionFactory::Configを継承して、qrpc_media_config_tを保持できるようにする

qrpc_media_config_tの値を使って、必要なペイロードをwebrtc::Clientが送るwhipリクエストのペイロードに追加してください。

=======
今 SyscallStream のCall系関数にはmsgidを必要とするものがあり、そのmsgidは外から渡されています。しかし、msgidは一意でなければならず、外部から渡すと一意性が維持されない不具合を生む可能性があります。したがって、以下のように修正します。

- Clientが持っている IdFactory<qrpc_msgid_t> msgid_factory_; をSyscallStreamに移動させ、msgidが必要なSyscallStreamの関数が呼ばれる場合、msgid_factory_から自分でmsgidを生成して使うようにする

=======
lib/tests/e2e/qrpc/server/main.cpp に lib/qrpc.h で定義されたAPIを使って lib/tests/e2e/core/server/main.cpp の base::webrtc::AdhocListener と同等の動作をするサーバーを作成してください。

bash lib/tests/e2e/core/run.sh native webrtc_client がexit 0で終了するように実装する必要があります。

もし lib/qrpc.h のAPIを誤って使ったことによってエラーになった場合、LLMがそのような誤った使い方をしないようにコメントも修正してください。

=======
lib/tests/e2e/core/run.sh のサーバーを起動する部分と、trapでサーバーをシャットダウンさせる部分を他のスクリプトでも使いたいので lib/tests/tools/debugger.sh に setup_server() のような関数として移動させてください。

=======
lib/tests/e2e/qrpc/client/main.cpp に lib/qrpc.h で定義されたAPIを使って lib/tests/e2e/core/client/native/main.cpp と同等の動作をするプログラムを作成してください。テストは lib/tests/e2e/qrpc/run.sh が成功するかどうかで行います。
lib/tests/e2e/qrpc/run.sh は作成したばかりなので不具合があれば修正してください。

また、もし lib/qrpc.h のAPIを誤って使ったことによってエラーになった場合、LLMがそのような誤った使い方をしないようにコメントも修正してください。

10,000,000,000

=======
qrpc_stream_handler_t::stream_reader, stream_writerを廃止します。理由は現在のtransportプロトコルであるsctp自身が送信したバイト列の単位でレコード境界を定義しているためです。

まず、どのような修正が必要か調べてください。

=======
lib/qrpc/stream.qrpc.cppにおいて、CodedByteStreamを廃止しましたが、これを残すようにしようと思います。
理由は、transportがTCPのようにメッセージ境界を持たない実装になるケースにも将来的に対応したいからです。

以下のようにします。
- lib/base/conn.h のインターフェイスに virtual bool has_message_boundary() const = 0;を追加する
- lib/base/webrtc.h では return trueとして実装する
- lib/qrpc/transports/webrtc.hのNewStream では has_message_boundary() のtrue/falseに基づいて、以下のようにStreamを作る
  - has_message_boundary() == true && type: STREAM => ByteStream
  - has_message_boundary() == false && type: STREAM => CodedByteStream
  - has_message_boundary() == true && type: RPC => RPCStream
  - has_message_boundary() == false && type: STREAM => CodedRPCStream
- ByteStream => 今のByteStream
- RPCStream => 現在のRPCStreamから、base::LengthCodec::Decodeと長さが足りない場合のバッファリング処理を除く
- CodedByteStream => 削除したCodedByteStreamと同じもの
- CodedRPCStream => CodedByteStreamで１レコードが得られた後、そこにbase::HeaderCodec::Decodeとそれ以降の処理を追加したもの(長さが足りない場合のバッファリングを除く)

=======

CodedByteStream::OnRead, CodedRPCStream::OnRead ですが、p, szが十分な長さの場合、parse_bufferにappendせずにそのままコールバックにポインタを渡すことができるはずです。このライブラリは小さなパケットを大量に送るようなユースケースが多いため、受信した時点でレコード長が足りているケースは多いです。
- CodedXXXStream::OnReadが呼ばれたときにparsed_buffer_.size() == 0 なら、p, szに対して直接base::HeaderCodec::Decodeで長さを取り出す。取り出された長さよりもszの残りが大きければそのままpと長さをコールバックに渡す。
- CodedXXXStream::OnReadが呼ばれたときにparsed_buffer_.size() > 0 なら、p, szをappendし、parsed_buffer_由来のバッファに対して同様の処理を実行する。
- base::HeaderCodec::Decodeで得られた長さに残りのバッファ長が足りない時のみparsed_buffer_にappendする(あるいは、すでにconsumeしたバッファ長分をeraseする)

=======

lib/ext/mediasoup/worker/src/RTC/SctpAssociation.cpp は、分割されたsctpメッセージを保持するためのバッファが１つしかない(this->messageBuffer)ため、以下の問題があります。

- sctp streamが複数存在し、それぞれのstream上でのメッセージが分割された際に、両方のメッセージの受信を正しく扱えない(破棄されてしまう可能性がある)
- unorderedなsctp streamが存在する場合に、後のメッセージが前のメッセージの受信完了前に届いてしまうと前のメッセージが破棄されてしまう

これを防ぐためにSctpAssociationのmessageBufferをstreamId,SSN,ppIdのタプルごとに保持するようにし、それぞれeorを受信したときに OnSctpAssociationMessageReceived をコールバックするようにしてください。ppid == 50のケースは別に扱い、OnSctpWebRtcDataChannelControlDataReceived を呼び出すようにします。

ppidごとにバッファを設ける必要はありますか？それともppid == 50 (dcep) とそれ以外を分けるだけでも良いでしょうか？意見を聞かせてください。

=======
lib/base/sig.hのAPIをqrpc.hに公開したいと考えていますが、以下のような考慮すべき点があります。
- sig.hのSignalHandlerはLoopが必要だが、qrpc.hにおいてはLoopはスレッドごとに作成される。SignalHandlerを担当するLoopをどのように選ぶべきか。
- あるいは、全スレッドでSignalHandlerを作成する。この場合、一度のsignalで複数スレッドのSignalHandlerが起動されてしまうか？

この点について意見を聞かせてください。

=======

SignalHandlerについては提案の通り、つまり、専用のスレッドを用意した上で、そこのLoopに対してSignalHandlerを追加して通知を受け取るようにします。
しかし、そのためには設計変更が必要です。
base::Loopは今、g_partition_id_とpartition_id_を保持しています。これはLoopがqrpcにおけるどのworker threadに所属しているかを表すものです。
qrpc_stream_t, qrpc_rpc_t, qrpc_conn_t, qrpc_media_t などのオブジェクトに操作を行う場合、partition_idを見て、同じpartitionからであれば直接操作を行い、そうでなければworkerのキュー経由でthread safeな形で操作を行う、というような動作をします。

base::LoopをWorkerを作らずに作成すると、「partition_idが割り当てられるが、workerがいないのでキューがない」という状況が発生し、今の前提が崩れます。
したがって、以下のようなある程度大きなリファクタリングを行う必要があります。実際、baseのレベルではpartition_idを考える必要はないため、このリファクタリングを行うことは自然だと思います。

- qrpc::Loopをbase::Loopを継承して作成し、g_partition_id_とpartition_id_の処理を移動させる。
- そうすると、シリアルの作成にはpartition_idが必要なため、baseのレベルではbase::Media, base::Stream, base::Connにシリアル(qrpc_serial_t)の処理を持たせられないため、これらをqrpcに移動させる
  - Stream => lib/qrpc/stream.h, Media => lib/qrpc/media.h, Connection => lib/qrpc/conn.h (継承してインターフェイスを追加する)

まずこのリファクタリングを行なってください。実装に入る前にあなたが理解したプランを提示してください。

=======
qrpc::Connection にメンバーを追加するとメモリーレイアウトが把握しづらくなるので以下のようにしたいです

- qrpc::Connectionはvirtually qrpc_serial_t serial() = 0追加するだけ
- 継承するクラスをテンプレート引数として受け取るtemplate class qrpc::ConnectionImplTを作成し。serial()の実装を含む今のqrpc::Connectionの残りの実装を追加する
- lib/qrpc/transports/webrtc.hのbase::webrtc::Listener::Connectionやbase::webrtc::Client::Connectionをqrpc::ConnectionImplTでラップする。

=======
SignalHandlerについては提案の通り、つまり、専用のスレッドを用意した上で、そこのLoopに対してSignalHandlerを追加して通知を受け取るようにします。
signalを扱うスレッドは、定期的なpollingを行わないようにし、さらにメモリの消費量もWorkerを保持するスレッドに比べて少なくします。
signal専用の初期化のために、qrpc_signal_init()を用意し、signal handlerを使いたい場合にはそれを呼び出す必要がある、という仕様とします

qrpc_signal_handleで登録されたハンドラーについては以下のように実装してください。

- qrpc::Loop::g_partition_id() が０ではない(qrpc::Loopを持つスレッド)場合、シグナルハンドラはqrpc_signal_handleを呼び出したスレッドでワーカーのキュー経由で呼び出される
- qrpc::Loop::g_partition_id() == 0であれば、signal handlerをあつかう専用スレッドの上で呼び出される

=======
いくつか指摘があります。
- スレッドがqrpc::Loopを持つ場合、それはそのスレッド上でWorkerが動くと考えて良いです。したがって qrpc::Loop::RegisterDispatcher, UnregisterDispatcherは不要で、qrpc::Worker::queue()にsignal handlerを渡せば良いと思います。
- base::LoopでブロックするPollを実現するためにメンバ変数を追加したり、Poll()内部で分岐したりしていますが、Pollを呼ばなければいけないわけではないので、base::Loop::WaitEventみたいな関数を作って呼び出す方が余計なオーバーヘッドを減らせます。同様にqrpc::LoopのXXBlocking系の関数も不要で、Pollのみ別関数を作れば良いです。
- base::Loop::WaitInfiniteですが、関数オーバーロードでWaitという同じ名前を使いたいです

=======

Worker::TaskQueueへのenqueueはすでにスレッドセーフなので、mutexを用意する必要はありません。
したがってWorker::queueの実装は元に戻してよく、またWorkerのqueueにタスクを積む場合も単にWorker::queue(partition_id).enqueue(...)とすれば良いです。したがって、g_queuesも不要なため、UnregisterQueue, RegisterQueueも削除できます。

=======
ありがとうございます。その指摘により、現在のServer::queue(partition_id)に欠陥があることに気づきました。この実装自体がServerが１つしか作られないという仮定のもとに行われていたようです。２個目以降のServerが作られるとpartition_idは1よりも大きな値で始まるため、 `TaskQueue &queue(PartitionId id) { return worker_queue_[id - 1]; }` では正しいqueueが取れない(それどころか境界外アクセスが起きる)です。また、複数スレッドがServerオブジェクトを作った場合、そもそもWorkerのpartition_idが連番になる保証もありません。したがって以下のように修正します。

- qrpc::Server::StartWorkerを読んでいるループをmutexで保護し、以下のように処理を行う
  - その時点でのg_next_partition_idの値を読み取り、Serverのメンバ変数(start_partition_id_)に格納する
  - StartWorkerをmutexで保護された状態で行う。これにより、このServerオブジェクトが所有するWorkerはg_next_partition_idから始まる連番になることが保障される
- Server::queue(PartitionId)は以下のような実装になる `TaskQueue &queue(PartitionId id) { return worker_queue_[id - start_partition_id_]; }`

その上で、あなたの質問については２の、登録時にTaskQueueも渡す、という方針で良いと思います。

=======
現在Loopがpartition_idを割り当てていますが、あらかじめreserveするのであれば、Serverにreserveする責務を負わせ、LoopはWorkerにセットされたpartition_idを受け継ぐ(自分ではpartition_idを生成したりしない)方が良いと思います。
- Serv
Worker::

=======
lib/tests/e2e/core/client/native/main.cpp でqrpcのrtpの動作を確認するテストを作成してください。以下のようなテストケースが必要です。
- test_webrtc_clientのような形でtest_rtp_clientを作成します。
- test_rtp_clientは２つのbase::webrtc::Clientを作成し、サーバーとの接続をそれぞれ行います。
  - １つのConnectionはbase::webrtc::Client::Connection::OpenMediaを行います。
    - qrpc_on_media_produce_tを実装する必要があります。著作権フリーの動画ファイルをダウンロードして、そのファイルを読み出して送信するような実装にしてください。ファイルが終端に達したら、先頭から再送信します。
  - もう一つのConnectionはbase::webrtc::Client::Connection::WatchMediaを行います。
    - 受け取ったrtpパケットから動画ファイルを生成して保存します。


もし lib/qrpc.h のAPIを誤って使ったことによってエラーになった場合、LLMがそのような誤った使い方をしないようにコメントも修正してください。

=======
qrpcのベンチマークをlib/tests/e2e/benchに作成します。サーバーとクライアントは別プロセスとし、クライアントには引数を渡してクライアント数をコントロールできるようにします。

=======
qrpcの再接続テストをlib/tests/e2e/reconnに作成します。


