### connection hijacking
- webrtc connectionはsessionに共有されている。
- sessionがwebrtc connectionと紐づくためにはstun packetを送信する必要がある
- stun packetから ConnectionFactory::FindFromStunRequest を使って対応するconnectionを取得する
- このリクエストのuser fragmentを意図的に他人のものにする攻撃が考えられる

- これについては、connectionが取得できた時点でpacketがuser fragmentに対応した認証情報を持っているかをそのconnectionを使ってチェックするようにしている (IceServer::ValidatePacket)
  - これにより、passwordを知らない攻撃者がufragのみを送ってもconnectionをhijackすることができないことが保証される
  - passwordはwhipリクエストのレスポンスとして適切に暗号化されてクライアントに返されるため、クライアントのメモリなどにアクセスしない限り攻撃者は知ることができない

- webrtc connectionが存在しないプロセスにstun packetが送られた場合は?
  - ufragの値に対応するwebrtc connectionが存在しない場合にはそもそもそのプロセスに接続することはできない
  - webrtc connectionは必ずwhipリクエストと共に作成されるが、その時に生成されるuser fragmentの値をコントロールすることはできない
    - できるとそれはそれで便利なのだが、安全に運用する方法は思いつかない
    - アプリケーションレベルのidは別に管理するしかないだろう

