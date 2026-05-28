### クライアントがやりたいこと
- produce: server側で($syscallストリームに対してjsonを送ることで)Producerを生成しておく。返されたanswerを見てclient側もProducerを生成する。
  - さらに現状のmediasoupの構造では、consumerが必要に見える。つまりclient側が自分自身のconsumerをそのproducerにアタッチした上で、rtp packetをなんらかの方法で作り、ReceiveRtpPacketを呼ぶことで、リモートのconnectionにRTCP付きでRTPパケットが送られそうな気がする
  - mediasoupのProducerというのは実際にパケットを生成するのではなく、パケットを受け付けるポートのようなもの

あとは、動画ファイルやカメラのキャプチャー、画面のスクショをどのようにrtp packetにするかが懸念点

- consume: 
  - もし指定したpathに存在してなければ、Producerをクライアント側のrtp::Handlerに登録しておく。与えられたコールバックを指定してConsumerを生成し、Producerに登録する.
  - server側で(clientから$syscallストリームに対してjsonを送ることで)PrepareConsumeを呼ぶ。これによりMediaStreamConfigが作成される
  - 接続が確立していればConsumeが一緒に呼ばれる。これによってConsumeが完了し、サーバー側からクライアントへconsumeしたストリームのrtpパケットが送られる

### サーバーがやりたいこと
- クライアントから送られてきたrtpパケットに対してコールバックを呼び出す
- サーバーが何かをproduce/consumeしたりはしなくて良いのか？ => サーバーはrouterに徹した方が良い。サーバーが何かをproduceする場合、それはクライアントを作ってそこからproduceすれば良いだけ。consumeしたい場合も同様

これらを踏まえたインターフェイスは
``` C
struct qrpc_media_produce_config_t {
  const char *path;
  struct {
    qrpc_on_media_produce_t source;
    bool paused;
  } audio, video;
};
struct qrpc_media_consume_config_t {
  const char *path;
  struct {
    qrpc_on_media_consume_t watcher;
    bool paused;
  } audio, video;
};
struct qrpc_media_config_t {
  // capabilities
  qrpc_media_params_t audio_cap, video_cap;
}
// generate default config
qrpc_media_config_t qrpc_media_config();
// initialize rtp feature, and set some config. eg. set capability
// only client connection need to call this.
void qrpc_conn_media_init(qrpc_conn_t c, qrpc_media_config_t *config);
// produce note that qrpc_on_media_produce_t called the thread which holds qrpc_conn_t.
// so be careful conflicts between the thread calls qrpc_conn_media_watch and 
// the one which calls qrpc_on_media_produce_t (eg. reading from camera or speaker)
qrpc_media_t qrpc_conn_media_open(qrpc_conn_t c, qrpc_media_produce_config_t *config);
// consume. same care as qrpc_conn_media_open is required (eg. for devices to play emitted rtp packet)
qrpc_media_t qrpc_conn_media_watch(qrpc_conn_t c, qrpc_media_consume_config_t *config)
// watch (mainly for watched media)
void qrpc_media_watch(qrpc_media_t m, qrpc_on_media_watch_t cb);
// close media. closed media slot can be used subsequent qrpc_conn_media_watch or qrpc_conn_media_open
void qrpc_media_close(qrpc_media_t m);
```

server側(SFU)はどうするのか？

- qrpc_conn_media_initは不要: clientが接続してきたときに初期化されるため。
- qrpc_conn_media_openは不要: clientが開くものであるため.
- qrpc_conn_media_closeは一旦利用できないが、将来的にはserverからstreamを閉じるために利用できるようになるかもしれない
