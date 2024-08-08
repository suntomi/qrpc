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
