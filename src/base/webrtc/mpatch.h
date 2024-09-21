#pragma once
#include "base/logger.h"
#include "base/assert.h"
// patch MS_ macros
// MS_THROW_ERROR
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

// MS_ERROR
#if defined(MS_ERROR)
	#undef MS_ERROR
#endif
#define MS_ERROR(...) \
	do \
	{ \
		QRPC_LOG(error, __VA_ARGS__); \
	} \
	while (false)

// MS_THROW_ERROR
#if defined(MS_THROW_ERROR)
	#undef MS_THROW_ERROR
#endif
#define MS_THROW_ERROR(desc, ...) \
	do \
	{ \
		std::snprintf(MediaSoupError::buffer, MediaSoupError::bufferSize, desc, ##__VA_ARGS__); \
		QRPC_LOG(error, MediaSoupError::buffer); \
		throw MediaSoupError(MediaSoupError::buffer); \
	} \
	while (false)

// MS_DEBUG_TAG
#if defined(MS_DEBUG_TAG)
	#undef MS_DEBUG_TAG
#endif
#if !defined(QRPC_DISABLE_MS_DEBUG)
	#define MS_DEBUG_TAG(tag, ...) \
		do \
		{ \
			QRPC_LOG(debug, __VA_ARGS__); \
		} \
		while (false)
#else
	#define MS_DEBUG_TAG(tag, ...)
#endif

// MS_DEBUG_2TAGS
#if defined(MS_DEBUG_2TAGS)
	#undef MS_DEBUG_2TAGS
#endif
#if !defined(QRPC_DISABLE_MS_DEBUG)
	#define MS_DEBUG_2TAGS(tag1, tag2, ...) \
		do \
		{ \
			QRPC_LOG(debug, __VA_ARGS__); \
		} \
		while (false)
#else
	#define MS_DEBUG_2TAGS(tag1, tag2, ...)
#endif

// MS_WARN_TAG
#if defined(MS_WARN_TAG)
	#undef MS_WARN_TAG
#endif
#define MS_WARN_TAG(tag, ...) \
  do \
  { \
    QRPC_LOG(warn, __VA_ARGS__); \
  } \
  while (false)

// MS_WARN_2TAGS
#if defined(MS_WARN_2TAGS)
	#undef MS_WARN_2TAGS
#endif
#if !defined(QRPC_DISABLE_MS_DEBUG)
	#define MS_WARN_2TAGS(tag1, tag2, ...) \
		do \
		{ \
			QRPC_LOG(warn, __VA_ARGS__); \
		} \
		while (false)
#else
	#define MS_WARN_2TAGS(tag1, tag2, ...)
#endif

// MS_TRACE
#if defined(MS_TRACE)
	#undef MS_TRACE
#endif
#if !defined(QRPC_DISABLE_MS_TRACK)
	#define MS_TRACE TRACK
#else
	#define MS_TRACE()
#endif

// MS_ASSERT
#if defined(MS_ASSERT)
#undef MS_ASSERT
#endif
#define MS_ASSERT MASSERT

// MS_ABORT
#if defined(MS_ABORT)
	#undef MS_ABORT
#endif
#define MS_ABORT(...) \
	do \
	{ \
		QRPC_LOG(fatal, __VA_ARGS__); \
	} \
	while (false)

// MS_DUMP_DATA
#if defined(MS_DUMP_DATA)
	#undef MS_DUMP_DATA
#endif
#define MS_DUMP_DATA(data, len) { logger::debug({{"hex",str::HexDump(data, len)}}); }

// MS_DEBUG_DEV
#if defined(MS_DEBUG_DEV)
#undef MS_DEBUG_DEV
#endif
#if !defined(QRPC_DISABLE_MS_DEBUG)
	#define MS_DEBUG_DEV TRACE
#else
	#define MS_DEBUG_DEV(...)
#endif

// MS_DEBUG_TAG
#if defined(MS_DEBUG_TAG)
#undef MS_DEBUG_TAG
#endif
#if !defined(QRPC_DISABLE_MS_DEBUG)
	#define MS_DEBUG_TAG(tag, ...) \
  do \
  { \
    QRPC_LOG(debug, __VA_ARGS__); \
  } \
  while (false)
#else
	#define MS_DEBUG_TAG(tag, ...)
#endif
