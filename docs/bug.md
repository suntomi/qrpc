1. xyzwを開く
2. xyzwのpublished media(webcam)をcloseする(もう一つのsubscribeも閉じるはず)
3. xyzwをリロード
4. xyzwのpublished media(webcam)をcloseする
5. xyzwで新しくpublishする(webcam2)
6. `no suitable Producer for received RTP packet` が発生

リロードしなければ6は起きない

cnameが固定でなくても起きるように見える

１月前の状態でも起きているので元々の問題であろう

6の状態のストリームは見れているのか？ => 見れているが、さらにpublished mediaをcloseして作り直すと見れなくなるようだ


エラーなく処理されているストリームでもmid/ridが全くセットされてないものが来ることがある => ssrcで探していると考えられるが、どのタイミングでmid/ridが来なくなるのか？


２つ目のブラウザクライアントが、最初以降mid/ridを送ってきてないように見える。その状態でclose => openすると、最初からmid/ridを送らないため受付先のrtp streamが作成されず、エラーになる

修正案：closeするときにridを保持しているものの場合、ridとssrcのマッピングを覚えておき、reuseされたときにRTPListener.ssrcTableにもproducerのエントリーを足しておく

この修正で`no suitable Producer for received RTP packet` は修正できるが依然として、srtpのkeyのミスマッチが起こる。



---------
srtpのkeyはマジで糸口が掴めないので、producerを閉じようとする試みを止める。つまり:
- 一度開いたclientのsenderのtransceiverは常に有効(/ch/1/video, /ch/2/audio みたいな汎用の名前でproducerを作る)
- 閉じたい場合はtransceiverをpauseして通信を止め、サーバーはconsumersに通知を行うだけにする
- 新しいstreamを送りたい時に、既存のtransceiverでcloseされているものがあれば、そのtransceiverのtrackをreplaceする(sdp)


----------
- ２〜３分待ってから、closeMediaし、再度openするだけでsrtpエラーは起きる => muteするようにしたからかも
- closeMediaしなくても2~3分待ってからsdpをセットするだけでも起きるか？ => 起きないようだ
- closeMediaの時、muteしなかったら大丈夫だろうか => muteしてもだめ
  - つまり、ある程度packetを送ったtransceiverが一度inactiveになってから再度activeにされるとsrtpのkeyが変わっているように見える
  - muteしなくてもinactiveにはなってしまう => trackがgcされるのを検出してinactiveにしているのかも
- inactiveにしないようにしても、close => openでreplaceTrackしたらsrtp keyエラーが起きている。シンプルなreplaceTrack(updateMediaで使われており、特に問題がない)と何が違うのか？ 
  => sdpがセット(setRemoteDescription/setLocalDescription)されているかどうか。close => openの流れでsdpのsetを避けることは可能だと思うが、close => open => new openの流れではどうしてもsdpをセットしなくてはいけないため、避けることが難しいだろう
  => だが、inactiveにせず、track idも変えないため、close => openの流れでは一切trasceiver modifiedが発生していないのだが、なぜsrtp errorになるのか？
  => closeMediaしないでopenMediaでmediaを追加した時のsdpのsetXXXDescriptionとの違いは何か(この場合は2分程度待っても問題は起きない)
- transceiverそのものを一度削除したらどうなのか => 削除はできないらしい


現状の解決策としては２つ
1. senderはcloseできない
2. closeできるがそのmedia sectionは二度と再利用しない 

-----------
- srtp エラーが出てもsubscribeができることがある...
- srtp エラーは例えばsimulcastでは特定のstreamに対して発生しているように見える
  - r0に対応するストリーム(一番画質の高いやつ)で起こる
- seqの値が維持されたままなので、問題が起きている説 => おそらくこれ。特にrocの値がリセットされていないため。
  - closemediaすると、streamが全て閉じ、replay dbもリセットされる。したがって、送られてきたpacketのseqnumからestimateされるpacket indexはseqnumそのものになる。当然roc = 0となる
  - しかし、クライアント(chrome)側ではどうやらrocがリセットされない状態でauth tagが計算されている模様
  - したがって、auth tagが合わずにエラーになる
  - rocが増えるのが最も早いのが、最も高品質なストリームであるため、r0だけが常にauth errorしていた
- よって修正のためには少なくとも、video streamについてはclosemediaでproducerを閉じてはいけない。chromeはssrcを維持して送ってくるため、次のstreamに再利用されてもそのまま使えば良い
- 確かにproducerを閉じなくても修正できるが、実際はrocがずれているだけだったので、rocだけをproducerが閉じるときに保存しておき、その後decrypt errorが出たら、その値で新しくstreamを作ってあげればそれだけで修正できる
  - producerを閉じないと、異なるbandwidthやscalabilitymodeを設定しなおせないので、producerを閉じれるこちらの方法の方が良い


auth tagの計算に使うestは

元々の値 = 0000 0000 0001 2cc1
=> (<<16)
0000 0001 2cc1 0000
=> (to big endian)
0000 c12c 0100 0000

として計算されている。

