配信するstreamのtrackをaddTransieverする時にenconding設定で必ずridを追加する。ridがかならずRtpStreamに乗ってくるか試してないが、
乗ってくるのであれば、それを使ってstreamのグループ化を行うことができる。ブラウザの実装がRtpStreamのridを省略するのであれば、少なくともSDPの交換の時に
ridとssrcの対応関係がわかるような情報を送ってきてくれるはず。いずれにせよtrackを追加したらrenegotiationは必要なので行う。

---------
RtpPaacketの処理フロー
WebRtcTransport::OnRtpDataReceived
RTC::Transport::ReceiveRtpPacket
 - SetXXXXExtensionId みたいなやつがたくさん呼ばれる。おそらくsdpのa=extmapのいくつかに対応している
 - この値はどのようにして決まるのか？
 - あとはproducerに流している.
Producer::ReceiveRtpPacket
  Producer::PreProcessRtpPacket
  Producer::PostProcessRtpPacket
Transport::OnProducerRtpPacketReceived
Router::OnTransportProducerRtpPacketReceived
SimpleConsumer::SendRtpPacket
  RtpStreamSend::ReceivePacket
    RtpStream::ReceiveStreamPacket
  Transport::OnConsumerSendRtpPacket
  WebRtcTransport::SendRtpPacket

結論、そのまま流している感じだが、これは正しいのか？
細かくproducerを分けておいて、クライアントが欲しい条件(eg. 画質/fps)に対するproducerをlistenする、みたいな感じなのだろうか。
しかし、その場合、それぞれのクライアントがkeyframeをリクエストしてきたりしたらどうするのか。あるいはNackとかの処理は？
- payloadがサポートされてない場合: そのクライアントには送られない

---------
QRPClient.onopen


もしかすると、singaling経由で受け取った情報でproducerを作成し、そこにconsumerを繋げていくという感じなのかもしれない
clientがconsumeしたいセッティングでproducerをserverに作成し(eg. signalingされたsdp parameter経由で)、改めてそれをconsumeする、みたいな感じ

見た感じ、transportに接続し、producerを作成するのは、qrpcでいうところの、dtlsで接続をestablishするのと、rtp/rtcpについて適切な設定を受け取ることに対応している。

sdpの交換時にそれら全ての情報は受け取られる(mediasoupの例ではdtlsパラメータとrtp/rtcpのパラメーターを交換するのは別のsignaling messageになっている)ため、qrpcでクライアントが接続する場合には、この両方の設定をすでに行なっている必要があるだろう。
つまり、sdpからdtlsのfingerprintを取り出してセットしたようなことを、rtp/rtcpについても行う必要がある(eg. extmapをパースして、SetXXXXExtensionIdにセットするidを記録しておくなど)

多分それでbase::webrtc::ConnectionFactory::ConnectionはmediasourpのTransport相当に動作するはず。

これはうまく動き、ridやmidが取れるようになった。

---------
実装

RTC::Sharedが邪魔なので消す。そのためにProducer/Consumer/Transportはこちらのソースツリーに移動させて修正する
Routerが必要かどうかよく分かっていない

Transport::ReceiveRtpPacketは本質的にはproducerにパケットを流し、処理結果によってパケットの受信統計情報をアップデートしているだけ

rtp.h/.cppがTransportのRtp/Rtcp処理部分を担当するようにする
ProducerはパケットをProducer::GetRtpStreamでストリーム(RtpStreamRecv)に振り分けている
ストリームはRtpPacketを受信したいConsumer向けに書き換えた(Producer::ReceiveRtpPacket)上で、ProducerをlistenしているConsumerに対して送信する
ConsumerはRtpStreamSendを持っていて、適切に送信を行う
rtp handlerがconsumerのマップを持つ

- rtpはwebrtcとは別にしておく rtp/handler.hがエントリーポイント handler = Transport
- handlerの初期化をsdpのパース時に行う。handlerからanswerを作ったほうがいいかもしれない。
- sdpからhandlerを作るときにProducerは作成する

- 一旦Producerがbrowserにフィードバックを返せるところまで作る。simulcastの全てのstreamが送信できるようになるところがゴール

- 


- podを超えてストリームを転送したいときには Producer -> PipeConsumer -> Producer とする必要がある?
- シリアライズした上で、TCP接続で平文でパケットを別podに転送した後Handlerに処理させる
- Connectionに紐づいていないHandlerが存在する

- subscribeしたい人
  - /qrpc/watch/:id にアクセスしてsdpを交換する
  - このときにすでに実際のProducerが存在している(= WebRtc接続がある)ノードとやりとりをしてHandlerが作成されている
    - ただし一旦は同じホスト上のもののみ
      - Handlerを作った時点で、user_idとHandlerのペアをどこかに登録しておく必要がある
      - 多分 `map<std::string, std::shared_ptr<rtp::Handler>> ConnectionFactory::channels_;` 的なやつ
      - /qrpc/watch/:id へのアクセスでは一旦、このmapのみを探す
    - 最終的にはcontrollerとやりとりをして:idに対応するwebrtc connectionを持っているノードを特定する
      - webrtc::ListenerはTcpClientとTcpServerを持つ必要が出てくるだろう
      - ノード <-> ノード通信ではuser_idを先頭にもち、シリアライズされたrtp packetをくっつけたようなパケットでやり取りする
  - DTLSで接続し終わった後、Handlerに自らをConsumerとして登録する
  - 接続する人がいなくなってしばらく経ったらHandlerは削除される

  - transportにいるconsumerはこのwebrtc connectionをsubscribeしている奴ら
    - それがこのwebrtc connectionから送られてくるrtcp(feedback. eg. rr)もsubscribeしているが、ssrcがわからないとsubscribeできない
    - RTCPの各要素のssrcはそれに関連づけられたRTP streamのssrcと一致するため、例えばこのwebrtc connectionを通じてRTP streamを送信するようにしていれば、そのRTP streamのssrcを使ってconsumerを登録することはできそう
    - そうしたとして、consumerは何をするのか？
      - consumerはこのwebrtc connectionから読み取ったRTP packetを受け取る
      - つまりwebrtc connectionからパケットを受け取る側である
      - したがって、webrtc connectionに送ったパケットのフィードバックを受け取る、というのはよくわからない。受け取る側は基本的にパケットを送らないと考えられるからである。
      - consumerが同時にRTP streamを送ることがあるのだろうか？
      - その場合、consumerを作成したwebrtc connectionが作成しているproducerを現在のwebrtc connectionがsubscribe(consume)するのではないのか？
      - mediasoup自体はclientではreceiver/senderで別々のRTCPeerConnectionを作っているようだ
      - sender(client側でいうreceiver)に対応するtransportでは確かにclientからRtcpが飛んでくるだろう
      - しかし、それに対してどうやってRtpPacketを送っている。。。？
        - consumerは別のRTCPeerConnectionにつながっている。そのconsumerを別のwebrtc connectionに追加することで送っているようだ
        - consumerを同じwebrtc connectionから作ってsubscribeしたいwebrtc connectionに追加しても大丈夫かもしれない
          - ssrcとかが大丈夫なのかは謎
          - とりあえず試す

- rtpMappingのssrcはランダムに作成される模様

- ProducerのRtpParameterはsdpから作成される(multiple codec含む)


- sdptransformを改造して、sdpをパースしたときにProducer/Consumerのコンストラクタに渡すべきjsonになるようにする or sdpのパース結果をRtpParameterにコピーする
- RtpParameterからsdpのanswerを生成するようにする
- RtpParameterはaudio/videoごとに別途作成するようにする(どこに保持するのか？) => producerをその時点で作成するか？


producerとmediaの関係をどうするか
media => webrtc::Connectionに持たせたい。on_open/on_closeをmediaごとの単位で呼びたい。on_readも呼びたい
producer => rtc::Handlerに持たせたい。


$user_id/


今のところまだ謎なこと
- media sourceが異なる複数のvideo streamを送る場合、midは複数になるのか？
  - simulcastは複数のストリームを１つのmidで送っている。これから類推するとmedia sourceが異なっていても、同じmidで送ることは原理的には可能に思える(ridやssrcが違うので)
  - しかし、media streamが違う場合は異なるストリームになるかもしれない
  - copilot chatはその感じで答えた
- ms毎にmidが異なる場合、labelはmidとイコールになるため、whipの時とかにマッピング関係を送れたらいいが、

POST https://host/qrpc/consume/:peer_id => :peer_idのpeerがproduceしている全てのmedia streamのvideo&audio trackをconsumeする
POST https://host/qrpc/consume/:peer_id/:lebel => :peer_idのpeerがproduceしている:labelというラベルを持つmedia streamのvideo&audio trackをconsumeする
POST https://host/qrpc/consume/:peer_id/:label/(video|audio) => :peer_idのpeerがproduceしている:labelというラベルを持つmedia streamのvideo|audio trackをconsumeする(選択されてない方はpauseで始まる)

GET https://host/qrpc/consume/:peer_id => :peer_idのpeerがproduceしているlabelの一覧


producerのid定義
/:id/:label

これでどんなtrackもconsumeできる(video/audioは問答無用でconsumeして、pauseするようにする)




リモートでconsumeしたtrackが論理的にどのストリームに当たるか
consume対象のpeerはtrackId label mapやrid label mapを持ってはいる
しかしridはかぶる(peer同士が同じようなプログラムで動いているとして)
ssrcは指定できる(rtpMappingで)が、


openMedia(id)で開く。video/audioのどちらかをpauseするならjsonでオプションをわたす
ssrcをconsumeの戻り値として渡す。そのssrcが来たら、onopenを呼ぶ

クライアントはconsumeしたトラックについてはonopen/oncloseしか呼ばない(onread的なものは呼べるのか？)。produceしているトラックについては、プロセッサを追加できる
サーバーはonreadも呼ばれる


consumerはrtpCapabilityとproducerの送信するencodingの組み合わせから、どのencodingとpayload typeを受信するかを判定し、rtpParameterを作成する


msがサポートしてないcodecは完全にフィルターされるが、その際に対応するrtxもフィルターしないといけない
