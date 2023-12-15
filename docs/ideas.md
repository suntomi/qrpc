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
例えばlabelに使うプロトコル以外の情報を埋め込みたい可能性は高い(並列でファイルを送りつけるときにプロトコルは全て同じファイル転送だが、ファイル名などの情報を埋め込みたいとか)
なので、?以下をパラメータとして、chat:write?id=hogeみたいな感じにするのがいいのかもしれない。プロトコルとのマッチングは?以下を無視する。
それともラベル名とストリームプロトコルのマッチング自体をカスタマイズ可能にしておき、デフォルトでは文字列としての完全マッチ、みたいにしてもいいかも