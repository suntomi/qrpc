```
namespace RTC 
{
=>
namespace base {
namespace msrtp {
  using namespace RTC;
```

```
} // namespace RTC
=>
} // namespace ms
} // namespace base
```

```
#include "RTC/Shared.hpp"
#include "handles/UnixStreamSocket.h"
#include "Channel/ChannelSocket.hpp"
#include "Channel/ChannelNotifier.hpp"
#include "PayloadChannel/PayloadChannelSocket.hpp"
#include "PayloadChannel/PayloadChannelNotifier.hpp"
#include "Logger.hpp"
=>
#include "base/rtp/ms/shared.h"
#include "base/rtp/ms/handles/UnixStreamSocket.h"
#include "base/rtp/ms/Channel/ChannelSocket.h"
#include "base/rtp/ms/Channel/ChannelNotifier.h"
#include "base/rtp/ms/PayloadChannel/PayloadChannelSocket.h"
#include "base/rtp/ms/PayloadChannel/PayloadChannelNotifier.h"
#include "base/rtp/ms/Channel/ChannelSocket.h"\n#include "Logger.hpp"
```

```
ChannelRequest.cpp, PayloadChannelNotification.cpp: add #include "base/webrtc/mpatch.h" after #include "Logger.hpp"
```

```
RTC::Shared or RTC::Producer or RTC::Consumer or RTC::SimulcastConsumer
=>
ms::Shared or ms::Producer or ms::Consumer or ms::SimulcastConsumer
```