consume用の別のwebrtc connection(CC)を作る
CCはmedia_path => consume option & consume parameter的なmapを持っている(consumer_config_map_)
consume syscallが呼ばれた場合に、primary connection(PC)はCCを作成するためにPrepareConsumeを呼ぶ
PrepareConsumeは以下を行う
- consumeするターゲットとなるPC(TPC)をmedia_pathから探す
- PCのproducerとTPCのproducerからconsume parameterを作る。ここでmappedSsrcも初期化される
- CCを作成する。ここでCC用のufragやpwdも作られる。rtpはデフォルトで初期化する。いろんなmapたちはPCからコピーしておく
- CCに作ったconsume parameterやsyscallから受け取ったconsume optionもmedia_pathとペアでセットする
- consumer paramsとCCからSDPを作る。
consume syscallはPrepareConsumeで作られたsdpをクライアントに返す
クライアントはsdpを元にconsume用peer connectionを初期化

一旦ここまでで、クライアントがちゃんと接続してきてくれるか試す。普通に考えると `ICE-CONTROLLED in STUN Binding request ` が起きてしまいそうなのだが
まだ `ICE-CONTROLLED in STUN Binding request `が起きるのであれば、さらにmediasoup-demoの研究が必要

クライアントが接続してきたら、rtp_handler_->Connected();してるあたりでconsumer_config_map_の内容を見てConsumeを呼び出す。
- 今は直接producerとかを引数にとっているが、consumer_config_map_のconsume parameterやconsume optionを使いたいため、リファクタリングが必要

prepareconsumeとconsumeを、すでに作られたconsumerに対してはidempotentとする
- consumer_connectonはnullでない時のみ作成する
- midごとにすでに存在しているかチェックし、ない場合のみconsumeする


test
- consumeできる
- 同じやつを２回consumeしても大丈夫(変化なし)
- 3つ以上consumeできる


pause/resume
- labelからproducer/consumerを探して操作する
- producer/consumerのidはconnection idを使っている(cnameではない)
- cname => connection idの検索が必要
- cnameを使わない理由は、cnameが一緒だったとしても、その中身のストリームはもはや一緒ではなく、中身が変わった段階で、それをみているクライアントは新しいストリームに繋ぎ直す必要があるため。
- 再接続時に古いconnectionを探して切断する。その際に切断の通知が各みているクライアントに届くはず。それをみて適切にconsumeしなおせば問題ない
- cnameのエントリーがすでに存在している場合に



watchStream
- 許可を与えて作成した誰かのstreamの送受信ペイロードを受信できるstreamを作成する
- pathをどうするか $watch/${cname}/${stream_path}/send|recv かな？
- 普通のstreamとの区別。$syscallみたいに$をつけるか