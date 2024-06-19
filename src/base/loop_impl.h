#pragma once

#include "qrpc.h"
#include "base/defs.h"
#include "base/syscall.h"

#if defined(__ENABLE_EPOLL__)
#include <sys/epoll.h>
#include <sys/types.h>

#if !defined(EPOLLRDHUP)
#define EPOLLRDHUP 0x2000
#endif
#if defined(__ANDROID_NDK__) && !defined(EPOLLONESHOT)
#define EPOLLONESHOT (1u << 30)
#endif

namespace base {
namespace internal {
	class Epoll {
	protected:
		Fd fd_;
	public:
		constexpr static uint32_t EV_READ = EPOLLIN;
		constexpr static uint32_t EV_WRITE = EPOLLOUT;
  #if !defined(LOOP_LEVEL_TRIGGER)
    constexpr static uint32_t EV_ET = EPOLLET;
  #else
    constexpr static uint32_t EV_ET = 0;
  #endif
		typedef struct epoll_event Event;
		typedef int Timeout;
		
		Epoll() : fd_(INVALID_FD) {}

		Fd fd() const { return fd_; }
		
		//instance method
		inline int Open(int max_nfd) {
			if ((fd_ = ::epoll_create(max_nfd)) < 0) {
				TRACE("ev:syscall fails,call:epoll_create,nfd:%d,errno:%d", max_nfd, Errno());
				return QRPC_ESYSCALL;
			}
			return QRPC_OK;
		}
		inline void Close() { Syscall::Close(fd_); }
		inline int Errno() { return Syscall::Errno(); }
		inline bool EAgain() { return Syscall::EAgain(); }
		inline int Add(Fd d, uint32_t flag) {
			Event e;
			e.events = (flag | EV_ET | EPOLLRDHUP);
			e.data.fd = d;
			return ::epoll_ctl(fd_, EPOLL_CTL_ADD, d, &e) != 0 ? QRPC_ESYSCALL : QRPC_OK;
		}
		inline int Mod(Fd d, uint32_t flag) {
			Event e;
			e.events = (flag | EV_ET | EPOLLRDHUP);
			e.data.fd = d;
			return ::epoll_ctl(fd_, EPOLL_CTL_MOD, d, &e) != 0 ? QRPC_ESYSCALL : QRPC_OK;
		}
		inline int Del(Fd d) {
			Event e;
			return ::epoll_ctl(fd_, EPOLL_CTL_DEL, d, &e) != 0 ? QRPC_ESYSCALL : QRPC_OK;
		}
		inline int Wait(Event *ev, int size, Timeout &to) {
			return ::epoll_wait(fd_, ev, size, to);
		}

		//static method
		static inline void InitEvent(Event &e, Fd fd = INVALID_FD) { e.events = 0; e.data.fd = fd; }
		static inline Fd From(const Event &e) { return e.data.fd; }
		static inline bool Readable(const Event &e) { return e.events & EV_READ; }
		static inline bool Writable(const Event &e) { return e.events & EV_WRITE; }
		static inline bool Closed(const Event &e) { return e.events & EPOLLRDHUP; }
		static inline void ToTimeout(uint64_t timeout_ns, Timeout &to) { to = (timeout_ns / (1000 * 1000)); }
	private:
		const Epoll &operator = (const Epoll &);
	};
}
typedef internal::Epoll LoopImpl;
}

#elif defined(__ENABLE_KQUEUE__)

#include <sys/event.h>
#include <sys/types.h>

namespace base {
namespace internal {
	class Kqueue {
	protected:
		Fd fd_;
	public:
		constexpr static uint32_t EV_READ = 0x01;
		constexpr static uint32_t EV_WRITE = 0x02;
  #if !defined(LOOP_LEVEL_TRIGGER)
    constexpr static uint32_t EV_ET = EV_CLEAR;
  #else
    constexpr static uint32_t EV_ET = 0;
  #endif
		typedef struct kevent Event;
		typedef struct timespec Timeout;

		Kqueue() : fd_(INVALID_FD) {}

		Fd fd() const { return fd_; }

		//instance method
		inline int Open(int max_nfd) {
			fd_ = ::kqueue();
			return fd_ < 0 ? QRPC_ESYSCALL : QRPC_OK;
		}
		inline void Close() { 
			if (fd_ != INVALID_FD) { 
				Syscall::Close(fd_); 
				fd_ = INVALID_FD;
			}
		}
		inline int Errno() { return Syscall::Errno(); }
		inline bool EAgain() { return Syscall::EAgain(); }
		inline int Add(Fd d, uint32_t flag) {
			return register_from_flag(d, flag, EV_ADD | EV_ET | EV_EOF);
		}
		inline int Mod(Fd d, uint32_t flag) {
			Del(d);
			return register_from_flag(d, flag, EV_ADD | EV_ET | EV_EOF);
		}
		inline int Del(Fd d) {
			//　try remove all flag and ignore error (if not set, -1(ENOENT) is returned from kevent)
			register_from_flag(d, EV_READ | EV_WRITE, EV_DELETE);
			return QRPC_OK;
		}
		inline int Wait(Event *ev, int size, Timeout &to) {
			return ::kevent(fd_, nullptr, 0, ev, size, &to);
		}

		//static method
		static inline void InitEvent(Event &e, Fd fd = INVALID_FD) { 
			e.filter = 0; e.flags = 0; e.ident = fd; 
		}
		static inline Fd From(const Event &e) { return e.ident; }
		static inline bool Readable(const Event &e) { return e.filter == EVFILT_READ; }
		static inline bool Writable(const Event &e) { return e.filter == EVFILT_WRITE; }
		/* TODO: not sure about this check */
		static inline bool Closed(const Event &e) { return e.flags & (EV_EOF | EV_ERROR);}
		static inline void ToTimeout(uint64_t timeout_ns, Timeout &to) {
			to.tv_sec = (timeout_ns / (1000 * 1000 * 1000));
			to.tv_nsec = (timeout_ns % (1000 * 1000 * 1000));
		}
	private:
		const Kqueue &operator = (const Kqueue &);
		inline int register_from_flag(Fd d, uint32_t flag, uint32_t control_flag) {
			int r = QRPC_OK, cnt = 0;
			Event ev[2];
			if (flag & EV_WRITE) {
				EV_SET(&(ev[cnt++]), d, EVFILT_WRITE, control_flag, 0, 0, nullptr);
			}
			if (flag & EV_READ) {
				EV_SET(&(ev[cnt++]), d, EVFILT_READ, control_flag, 0, 0, nullptr);
			}
			if (::kevent(fd_, ev, cnt, nullptr, 0, nullptr) != 0) {
				r = QRPC_ESYSCALL;
			}
			return r;
		}
	};
}
typedef internal::Kqueue LoopImpl;
}

#else //TODO: windows
#error no suitable poller function
// dummy implementation to shut vscode language server up
class LoopImpl {
public:
	constexpr static uint32_t EV_READ = 0x01;
	constexpr static uint32_t EV_WRITE = 0x02;
	typedef struct {} Event;
	typedef int Timeout;
	Fd fd() const { return INVALID_FD; }
	inline int Open(int max_nfd) {
		return QRPC_OK;
	}
	inline void Close() {}
	inline int Errno() { return 0; }
	inline bool EAgain() { return false; }
	inline int Add(Fd d, uint32_t flag) {
		return QRPC_OK;
	}
	inline int Mod(Fd d, uint32_t flag) {
		return QRPC_OK;
	}
	inline int Del(Fd d) {
		return QRPC_OK;
	}
	inline int Wait(Event *ev, int size, Timeout &to) {
		return QRPC_OK;
	}

	//static method
	static inline void InitEvent(Event &e, Fd fd = INVALID_FD) {}
	static inline Fd From(const Event &e) {return 0; }
	static inline bool Readable(const Event &e) { return true; }
	static inline bool Writable(const Event &e) { return true; }
	static inline bool Closed(const Event &e) { return true; }
	static inline void ToTimeout(uint64_t timeout_ns, Timeout &to) {
		to = timeout_ns / (1000 * 1000);
	}
};
#endif
