#pragma once

#include "base/loop_impl.h"

namespace base {
	// TODO: should use function object instead of virtual function to avoid multiple inheritance in session.h?
	class IoProcessor {
	public:
		typedef typename LoopImpl::Event Event;
		virtual ~IoProcessor() {}
		virtual void OnEvent(Fd fd, const Event &e) = 0;
	};
}
