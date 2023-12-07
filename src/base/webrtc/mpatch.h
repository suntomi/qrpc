#pragma once
#include "base/logger.h"
#include "base/assert.h"
// patch MS_ macros
#if defined(MS_THROW_ERROR)
#undef MS_THROW_ERROR
#endif
#define MS_THROW_ERROR(desc, ...) \
	do \
	{ \
		std::snprintf(MediaSoupError::buffer, MediaSoupError::bufferSize, desc, ##__VA_ARGS__); \
    QRPC_LOG(error, MediaSoupError::buffer); \
		throw MediaSoupError(MediaSoupError::buffer); \
	} while (false)
#if defined(MS_ERROR)
#undef MS_ERROR
#endif
#define MS_ERROR(...) \
	do \
	{ \
		QRPC_LOG(error, __VA_ARGS__); \
	} \
	while (false)
#if defined(MS_DEBUG_TAG)
#undef MS_DEBUG_TAG
#endif
#define MS_DEBUG_TAG(tag, ...) \
	do \
	{ \
		QRPC_LOG(debug, __VA_ARGS__); \
	} \
	while (false)
#if defined(MS_WARN_TAG)
#undef MS_WARN_TAG
#endif
#define MS_WARN_TAG(tag, ...) \
  do \
  { \
    QRPC_LOG(warn, __VA_ARGS__); \
  } \
  while (false)
#if defined(MS_TRACE)
#undef MS_TRACE
#endif
#if !defined(QRPC_DISABLE_MS_TRACK)
#define MS_TRACE TRACK
#else
#define MS_TRACE()
#endif
#if defined(MS_ASSERT)
#undef MS_ASSERT
#endif
#define MS_ASSERT MASSERT
#if defined(MS_DUMP_DATA)
#undef MS_DUMP_DATA
#endif
#define MS_DUMP_DATA(data, len) { logger::debug({{"hex",str::HexDump(data, len)}}); }
#if defined(MS_DEBUG_DEV)
#undef MS_DEBUG_DEV
#endif
#define MS_DEBUG_DEV TRACE
