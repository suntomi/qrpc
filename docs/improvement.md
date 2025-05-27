- タイマーをconnection１つにつき１つにしたい(せめて遅いやつと早いやつの２つ)
- labelやscalability modeをサーバーで使うための変数を送りすぎている。減らす
  - xxx_label_map とか rid_scalability_mode_map_
- 可能なら接続を１つに
  1. ダミーのRTCPeerConnectionを作ってaddTranceiver("audio"|"video")することでcapabilityは十分なSDPが送信できる
  2. しかし、encodings相当は厳しい
  3. onopenでやるセットアップを遅延させて、どういうセットアップをするかの情報だけを受け取り、1で生成したSDPを書き換えてサーバーに送る、みたいな感じになりそうb




closeしたtrack用のsdp sectionを再利用する件について
ridが変更できない。なのでmidごとに利用するridを固定したい。

サーバー側でMediaStreamConfigに記録されているridを返して使う => scalability modeとの対応をどうするか？
scalability modeの配列を送って、前から順に入れる？