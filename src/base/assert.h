#pragma once

#if !defined(ASSERT)
  #if !defined(NDEBUG)
    #include <assert.h>
    #define ASSERT(cond) assert((cond))
    #define MASSERT(cond, ...) { if(!(cond)){ QRPC_LOG(fatal, __VA_ARGS__); assert((cond)); } }
    #define DIE(msg) assert((msg) == nullptr)
  #else
    #define ASSERT(cond)
    #define MASSERT(cond, ...)
    #define DIE(msg)
  #endif
#endif

#if !defined(STATIC_ASSERT)
#include <type_traits>
#define STATIC_ASSERT static_assert 
#endif
