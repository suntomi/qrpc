#pragma once

#include "base/loop_impl.h"

namespace base {
	class IoProcessor {
	public:
		typedef typename LoopImpl::Event Event;
		virtual ~IoProcessor() {}
		virtual void OnEvent(Fd fd, const Event &e) = 0;
		virtual void OnClose(Fd fd) = 0;
		virtual int OnOpen(Fd fd) = 0;
	};
}
