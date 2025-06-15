### RtpPaacketの処理フロー

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
