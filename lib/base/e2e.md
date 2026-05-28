# test cases
1. 最初からaudio/videoをpauseしてはじめることができ、その場合、lib/tests/e2e/core/server/resources/media.html のアプリでpublishした際にpause状態から始まるつまり、
  - publishした側も最初からpause状態の表示
  - consumeする側も最初からpause. publish側がresumeすると初めて動画が流れる.
2. chromeで複数のタブを開いて同じサーバーに対してconsume/produceしても、両方のタブのストリームを他のタブはconsumeできる。閉じて開いたり、produce側をリロードしても問題ない
3. 同じcname/pathである限り、リロードしても他のタブは同じ名前のストリームに（一度切断されるものの）特別に操作することなしに再接続できる
