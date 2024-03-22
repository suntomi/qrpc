### plans
- webrtcのtcp/udp session, stream, connectionはAllocatorから割り当てるようにする。同時にシリアルを入れる
  - qrpcのサーバーでは、他の接続へのrpc呼び出しにハンドルを使うことになるが、ポインタの中身が入れ替わる問題があるため、シリアルとポインタの中のシリアルの比較ですでに不正な状態になっていることを検出できるようにする
- tlsを使えるようにする
 - 既存のスタックのどこに入れるのか？
  - TcpSessionにはopensslを入れる
  - 本来UdpSessionには今使っているDtlsを導入して、webrtc特有のものにしないのが綺麗だが、sctp以外で使う用途があるだろうか
    - 暗号化しないsctpとか？
  - 一旦tcpsessionだけにopensslでtlsを入れるのがいいだろう

### done
- safariとfirefoxで動かないぞ
  - safari => dtls handshakeを始めない
    - serverからhandshakeするようにしたら動いた。latency的にもそっちの方がいい
  - firefox => stun requestを送ってこない
    - 127.0.0.1には送ってこなかった。getifaddrsとかでinterfaceのアドレスを取得してそのアドレスをsdpで返すようにしたら動いた
- webrtcserverをwebrtcconnectionfactoryから継承するようにして、webrtcconnectionfactoryが接続の作成だけできるようにする
  - qrpcにクライアント側も作成できるようにしておきたいため
