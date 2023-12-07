## udp packet受信の流れ
- libuv
- UdpSocketHandler::OnUvRecv
- UdpSocket::UserOnUdpDatagramReceived
- WebRtcServer::OnUdpSocketPacketReceived
- WebRtcServer::OnPacketReceived
- WebRtcServer::OnStunDataReceived, WebRtcServer::OnNonStunDataReceived
  - ここで受信元のアドレスを元にパケットがアドレスに紐づいたWebRTCTransportに振り分けられる
- WebRTCTransport::ProcessStunPacketFromWebRtcServer, WebRTCTransport::
- ここでSTCPデータの場合が我々が興味があるもの

## アドレスごとのWebRTCTransportはいつ作成されるか
- WebRtcTransport::ProcessStunPacketFromWebRtcServer or WebRtcTransport::OnStunDataReceived
- IceServer::ProcessStunPacket
- IceServer::HandleTuple
- IceServer::AddTuple
- WebRtcTransport::OnIceServerTupleAdded
- WebRtcServer::OnWebRtcTransportTransportTupleAdded

### WebRtcTransport::ProcessStunPacketFromWebRtcServerが呼ばれる流れ
- WebRtcServer::OnStunDataReceived
  - すでにtransportが作成されている場合ないし、パケットに含まれたIce userfragmentでmapLocalIceUsernameFragmentWebRtcTransportの中にwebrtctransportが見つかった場合(なぜSTUNに含まれているのか？)
  - usernamefragmentはice candidateとしてシグナリングしてきたクライアントに送られているはずだが、クライアントがSTUNリクエストを送るというのか？ => 送るということのようだ。iceで接続先が通信可能か調べるときにstunパケットを使う

### WebRtcTransport::OnStunDataReceived
- これは上の流れですでにtransportが登録された後のようだ

### mapLocalIceUsernameFragmentWebRtcTransportにtransportが追加される流れ
- 現状Routerにリクエストが飛んだ時に作成されている。おそらくシグナリング時に接続先を新しく作成するような想定になっているのだろうと思われる.
  - つまりportをシェアすることはできないということだろうか。
- WebRTCTransport::ctor
- IceServer::ctor
- IceServer::OnIceServerLocalUsernameFragmentAdded
- WebRtcTransport::OnIceServerLocalUsernameFragmentAdded
- WebRtcServer::OnWebRtcTransportLocalIceUsernameFragmentAdded
  - WebRtcServer::mapLocalIceUsernameFragmentWebRtcTransport にWebRTCTransportが追加される
