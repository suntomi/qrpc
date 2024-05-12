- k8sのクラスタでstatefulsetとして起動する
- statefulsetで起動するpod毎に個別のserviceを作成する
- statefulsetの番号を使って全podがユニークなportで起動する
  - 多分60000+から
- loadbalancerはport毎にstatefulsetの特定のpodにforwardする
- statefulsetがスケールアウトしpodが起動すると、operatorがそれを検出し、以下のステップを実行する
  - 起動したpodがrunningになるのを待つ(running = トラフィックを受付可能になるという意味)
  - 起動したpodがlistenするport(port-N)にトラフィックを転送するservice(pod-Nに対してsvc-Nみたいな名前)
  - aws load balancer controllerが監視しているingressへのエントリー追加(port-Nへのアクセスをsvc-Nに転送するような設定)
  - 起動したpodにload balancer経由でhealth checkを行い、OKが返ってくるのを待つ(stun binding request)
    - pod側は、/qrpcを提供するportのヘルスチェックについては、operatorが返すリストに自身が含まれるようになるまではOKを返さない。
- statefulsetがスケールインした場合でも別に上記の設定は削除しない。この場合、スケールインによってダウンしたpodについては/qrpcが転送されなくなるので、lbに設定が残っていたとしてもそもそもそいつにアクセスする奴がいなくなるため問題がない.
  - podが減る場合、その瞬間にシャットダウンするpodのメモリ上のフラグを立てて、/qrpcにリクエストが来ても自分自身は返さないようにしたい


