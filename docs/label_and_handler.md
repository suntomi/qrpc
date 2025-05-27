handler => stream/mediaで発生したイベントがどのように処理されるかを表す
label => stream/mediaそのものを表す。url、cname, handler、パラメーターからなる。。全てのstream/mediaでユニークである必要がある。その条件が満たされているなら、パラメータがなくても(つまりhandler名と同じ値のlabelでも)良い。(例えばmediaについては他のユーザーがconsumeする&各ユーザーについて１つずつしか用意されないため、他のユーザーからの指定を楽にするためにパラメータなしにされることが多い)QRPClientのコンストラクタで指定したurlと一致するならば、urlは省略できる。自分自身を表すのであれば、cnameは省略できる。urlを省略せずにcnameを省略することはできない

label = url/@cname/name # フルパス
label = cname/name # デフォルトのurlのqrpcサーバーに存在するstream/mediaを指定する
label = name # デフォルトのurlのqrpcに存在する自分のstream/mediaを指定する

考えてみれば、同じlabelのstream/mediaであってもアプリごとに使われ方は変わる気がする => だからhandleやhandleMediaには固有の処理をかけるわけだが
しかし複数のlabelが同じようにhandleされたいかもしれないし、逆にhandler部分が同じでも別に処理したいことがあるかもしれない。
よってmediaについてはlabelにhandlerが含まれているというのは良い考えではない。

streamについては、openStreamで開かれる名前でhandleされるというのは自然であると言える。しかしstreamについては、

そう考えると、openXXX自体にcallbackを与えるやり方の方がいいのかもしれない

labelを指定する場所: openStream, openMedia, consumeMedia (TODO: consumeMedia => viewMediaとする)

openStream(label, createDataChannelOptions | RTCDataChannelEventHandlers)
openMedia(label, { stream, encoding, onopen, onclose })
watchMedia(label, { onopen, onclose })

結論として、qprcのコアライブラリとして、stream/mediaのコールバックを定義する概念としてのhandlerはなくなった。labelそれぞれに対して適切なコールバックを割り当てるのはより上の層の責任になる。
qrpcのコードジェネレーターでは、serviceの名前をlabelとしてstream/mediaを開き、serviceに定義されているrpcを実装したハンドラをコールバックとして設定する、という形になる。

１つのmediaに種別がある(eg. audio/video)点をどうするか。つまりopenMediaではstreamが表現しているすべてのメディアをすべてpublishするのだが、その場合のpathはaudio/videoを含まないものになるべき。
つまり、directoryのような形のpathがあって、その下にpath/audio, path/videoのような形で実際にviewできるpathが作成されるイメージ。同様にviewMediaする場合、その下のtrackをすべてviewしたい場合は
openMediaしたときのpathを指定するのだが、もしかするとaudioのみviewしたいケースがあるかもしれない。この場合はpath/audioのように指定したい。

=> 結局directory pathを定義するのがよさそう。つまり、"${path}/" のように指定されるpathである。この場合、open/viewMediaでは以下のようになる
openMedia => streamにアタッチされているtrack全てについてpublishし、track種別ごとに "${path}/audio", "${path}/video" のようなpathを与える。"${path}/audio"のような指定をして、streamのうち１つだけをpublishするようなことはできず、openするstreamに幾つtrackが含まれているかでコントロールされる。また、${path}に/が含まれない場合は最後の/を省略できる。
watchMedia => この "${path}/" の下に存在するすべてのtrackをsubscribeする。openMediaと違い、 "${path}/audio" のようにすれば特定のtrackだけをsubscribeできる
