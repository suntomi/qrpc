### 方針
- DtlsTransport/IceServer/SrtpSessionあたりを使って自前のconnectionクラスを作る
- SctpAssosiactionも同じクラス内に持たせる
- eventloopは自前のやつ。TCP listener socketとUDP socketを作成して接続およびパケットを待ち受ける
- コネクションを識別するのはufrag
- WHIPでPOSTリクエストが来た時にconnection(のもと)を作成しておく。この時にIceServerも作成され、ufragが生成されるので、map<ufrag, connection> みたいな構造に保持しておく。
- クライアントはWHIPのレスポンスをもとにサーバーに接続してくる
- hole punchingのSTUNパケットに含まれるufragからconnectionを特定する。ufragの正しい保持者かどうかはpasswordを使ったhmacなどで検証されているみたい。
これで
  - UDP: map<remoteAddr, connection>
  - TCP: map<fd, conneciton>
みたいな構造に保持する。
- 以降はremoteAddrないし、受信したsocketで探す。
- 当初はSRTPには対応しない(しなくてもいいかもしれないので)
- よって受信するのはSTUN or DTLS
  - STUNはiceServerに、DTLSはDtlsTransportに流す