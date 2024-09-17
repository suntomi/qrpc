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
=>
#include "base/rtp/ms/shared.h"
#include "base/rtp/ms/handles/UnixStreamSocket.h"
#include "base/rtp/ms/Channel/ChannelSocket.h"
#include "base/rtp/ms/Channel/ChannelNotifier.h"
#include "base/rtp/ms/PayloadChannel/PayloadChannelSocket.h"
#include "base/rtp/ms/PayloadChannel/PayloadChannelNotifier.h"
```

```
RTC::Shared or RTC::Producer or RTC::Consumer or RTC::SimulcastConsumer
=>
msrtp::Shared or msrtp::Producer or msrtp::Consumer or msrtp::SimulcastConsumer
```