## inclientserverとcontainerの違い
### inclientserver
- inclientserverはinclientclusterから起動される。おそらくinclientclusterはシーンに前もって追加されている
- inclientserverはworkerを(Unity Editorと同じプロセス内の)別スレッドで起動し、client(Unity Editorのメインスレッド)からのメッセージをキューに積む。(use_queue = true)そして、Unity Editorのメインスレッドでキューからメッセージを取り出して処理する。
  - メッセージが来た場合、handlerのPoll/Close/Login/Handleが呼ばれるが、これらは全てmtk server.cppにあるServerクラスのqueueに積まれるように設定されているため、UnityEditor側からqueue経由でデータを取り出せる。

### container
- コンテナはmtksvランタイムを動かす。
- mtksvランタイムはUnity Projectに含まれているサーバーロジックを前もってビルドされた.net assemblyに変換したファイルをロードする(MonoHandler::Init)
  - ロジッククラスのBootstrap(string[])を呼び出してServerBuilder(設定済み)を受け取り、ServerBuilder.Buildを読んでサーバーを作成する。
  - さらにEntryPointクラスのLogicクラスを受け取るstaticメソッドPoll/Close/Login/Handleを取り出し、ネットワークイベントと紐づけている
  - 上記のメソッドをLogicクラスを与えて呼び出すことでinclientserverと同じように.netのコードを呼び出すことができる.
