## LoopとIoProcessorのライフサイクル
### 作成/Poll
- IoProcessorの継承クラスをnewする
- loop.Add
  - IoProcessor.OnOpen
- loop.Poll
  - IoProcessor.OnEvent

### 削除
- Closeの呼び出し
  - Close
- OnEventのエラー
  - 
- remoteからの切断
  - readが0bytesになる
