#pragma once

#include "qrpc.h"

namespace base {
	namespace clock {
		qrpc_time_t now();
		void now(long &sec, long &nsec);
		qrpc_time_t sleep(qrpc_time_t dur);
		qrpc_time_t pause(qrpc_time_t dur);
		inline uint64_t to_us(qrpc_time_t t) {
			return t / 1000;
		}
	}
}
