sctpのstreamのlabelはどのstreamをオープンするか、に使われる予定だが、色々使えるようにしたい。
任意のqrpcサーバーはenvoyのようなプロキシになりうるようにする。

qrpcはreusable&composableであることを目指すので、そもそもサーバー自身が持っているメソッドについてもmoduleによる名前空間があるようにしたい。
つまり、チャット用のRPCのモジュール `chat` なるものが、github.com/hoge/chat-qrpcというレポジトリに置かれており、そのRPC `send` を呼び出すためには `write` という名前の専用のストリームが構築されるとする。
その場合、サーバーに送られるsctpのラベルは
/github.com/hoge/chat-qrpc:write
となり、ストリームが作成されたらクライアントはこのストリーム経由でsendというRPCを送信する、という感じ。このようにすることで、例えばguild用のqrpcモジュール github.com/fuga/guild-qrpc があって、その呼び出しのためのストリーム `write`があったら
/github.com/fuga/guild-qrpc:write
のように区別してラベルを送ることができる。これにより、複数のモジュールが提供するRPCのスイートを無理なく共存できる。

ここまではすでに考えていたことなのだが、これに加えて、モジュールを組み込むのではなく、リモートのサービスをそのまま呼び出せるようにするのはどうかと考えている。つまり、上記の github.com/fuga/guild-qrpc をすでにサービス化したインスタンスが、 fuga.com に存在しているとする。この場合にラベルを fuga.com/github.com/fuga/guild-qrpc:write とすることで、qrpcサーバーは、fuga.comと接続を作成し、 github.com/fuga/guild-qrpc:write でストリームを作って、クライアントから送られてきたrpcをそこに対してプロキシするようにする。これによりマイクロサービスとしてRPCを切り分けていったり、外部サービスに委譲したりすることが簡単になる

多分
/github.com/fuga/guild-qrpc の部分は、名前をつける方が良い

qrpc.toml

[services]
chat = { provider = "localhost", src = "github.com/hoge/chat-qrpc#v1.0.2" }
guild = { provider = "fuga.com", service = "guild-sevice" }

みたいに定義するようにする。
そして、

chat:write
guild:write

みたいなラベルでストリームをopenする

chatの場合はローカルでビルドされて組み込まれたサーバーのソースコードでリクエストが処理されるし、guildの場合はfuga.comに(guild-sevice:writeとして)転送される
クライアントのソースは取ってこれないといけないな。provider = "fuga.com" の場合にどこからクライアントをビルドするためのソースを取ってくるかのルールが必要。
fuga.com/guild-service/spec みたいなところを叩くと ソースコードのレポジトリに301されるとかで良さそう。srcを別途指定するのでもいい。

circuit breakerとかfallbackとか、色々定義できると良さそうではある。ホットリロードできるかな？

ラベルの:以降にもいくつかのルールを設けるべきかもしれない。
例えばlabelに使うプロトコル以外の情報を埋め込みたい可能性は高い(並列でいくつかのでかいデータを分割して送りつけるときにプロトコルは全て同じだが、別々のストリームとして扱いたいときなど)
なので、?以下をパラメータとして、chat:write?id=hogeみたいな感じにするのがいいのかもしれない。プロトコルとのマッチングは?以下を無視する。
それともラベル名とストリームプロトコルのマッチング自体をカスタマイズ可能にしておき、デフォルトでは文字列としての完全マッチ、みたいにしてもいいかも

### for produce
$path is configurable, but typically qrpc for stream, qrpc/medias for media
stream label: $host/$path/@$name?$k1=$v1&$k2=v2...
media label : $host/$path/@$name?$k1=$v1&$k2=v2...

### for consume
$path is configurable, but typically qrpc for stream, qrpc/medias for media
stream label: $host/$path/@$id/$name?$k1=$v1&$k2=v2...
media label : $host/$path/@$id/$name?$k1=$v1&$k2=v2...


GET $host/$path/@$id => should return all labels available, correspondint $path
eg) GET $host/qrpc/@$id => ["app","filetx","chat"]
eg) GET $host/qrpc/medias/@$id => ["webcam","webcam2","ingame"]


### qrpc
qrpcのレベルでは、onopen/oncloseはトランスポートレベルのopen/closeを表さない
onopen => そのqrpc serviceの全てのprocedureを呼び出す準備が整った
onclose => そのqrpc serviceの内部もしくは外部からclose()が呼ばれた
例えば複数のconnectionを利用するqrpc serviceは一部のconnectionがcloseすることがあるが、これによってoncloseがemitされるわけではない。
もちろんqrpc serviceの作者が利用している特定のconnectionのcloseを見て全体のcloseを呼ぶこともできる。
あるいは再接続で問題が解決する場合、単純に接続が切断されている場合は何らかのエラーを返す、というようにして、qrpc service全体ではcloseしていないという状態にすることもできる。
そのためにemitを用意している。各childのqrpc serviceについてemit(event, param)を呼び出すことでlabel/event, paramという形でイベントが来るようにする
つまり、chatを配信するserviceへのconnectionがcloseした場合は、chat/closeみたいなイベントがemitされる

もしかすると全体をemitで作る方がいいのか。でもonopen/oncloseだけは共通なのでこのままいく。onopen onclose onevent の組み合わせ
