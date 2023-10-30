#pragma once

#if !defined(ASSERT)
  #if !defined(NDEBUG)
    #include <assert.h>
    #define ASSERT(cond) assert((cond))
    #define MASSERT(cond, msg) assert((cond) && (msg))
    #define DIE(msg) assert((msg) == nullptr)
  #else
    #define ASSERT(cond)
    #define DIE(msg)
  #endif
#endif

#if !defined(STATIC_ASSERT)
#include <type_traits>
#define STATIC_ASSERT static_assert 
#endif
