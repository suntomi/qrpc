#ifndef MS_UNIX_STREAM_SOCKET_HPP
#define MS_UNIX_STREAM_SOCKET_HPP

#include "common.hpp"
#include <uv.h>
#include <string>

namespace base {
namespace ms {
class UnixStreamSocket
{
public:
	enum class Role
	{
		PRODUCER = 1,
		CONSUMER
	};

public:
	UnixStreamSocket(int fd, size_t bufferSize, UnixStreamSocket::Role role) :
		bufferSize(bufferSize), role(role) {}
	UnixStreamSocket& operator=(const UnixStreamSocket&) = delete;
	UnixStreamSocket(const UnixStreamSocket&)            = delete;
	virtual ~UnixStreamSocket() {}
protected:
	virtual void UserOnUnixStreamRead()         = 0;
	virtual void UserOnUnixStreamSocketClosed() = 0;	
public:
	void Close() {}
	bool IsClosed() const { return false; }
	void Write(const uint8_t* data, size_t len) {}
protected:
	// Passed by argument.
	size_t bufferSize{ 0u };
	UnixStreamSocket::Role role;
	// Allocated by this.
	uint8_t* buffer{ nullptr };
	// Others.
	size_t bufferDataLen{ 0u };
};
} // namespace ms
} // namespace base

#endif
