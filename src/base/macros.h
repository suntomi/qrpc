#pragma once

#include <stddef.h>  // For size_t.

// Put this in the declarations for a class to be uncopyable.
#define DISALLOW_COPY(TypeName) \
  TypeName(const TypeName&) = delete

#define DISALLOW_MOVE(TypeName) \
  TypeName(TypeName&&) = delete

// Put this in the declarations for a class to be unassignable.
#define DISALLOW_ASSIGN(TypeName) TypeName& operator=(const TypeName&) = delete

// Put this in the declarations for a class to be uncopyable and unassignable.
#define DISALLOW_COPY_MOVE_ASSIGN(TypeName) \
  DISALLOW_COPY(TypeName);                 \
  DISALLOW_MOVE(TypeName);                 \
  DISALLOW_ASSIGN(TypeName)

// A macro to disallow all the implicit constructors, namely the
// default constructor, copy constructor and operator= functions.
// This is especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName() = delete;                           \
  DISALLOW_COPY_MOVE_ASSIGN(TypeName)

// The arraysize(arr) macro returns the # of elements in an array arr.  The
// expression is a compile-time constant, and therefore can be used in defining
// new arrays, for example.  If you use arraysize on a pointer by mistake, you
// will get a compile-time error.  For the technical details, refer to
// http://blogs.msdn.com/b/the1/archive/2004/05/07/128242.aspx.

// This template function declaration is used in defining arraysize.
// Note that the function doesn't need an implementation, as we only
// use its type.
template <typename T, size_t N> char (&ArraySizeHelper(T (&array)[N]))[N];
#define arraysize(array) (sizeof(ArraySizeHelper(array)))

/* memory access support */
#define GET_8(ptr)		(*((uint8_t *)(ptr)))
#define GET_16(ptr)		(*((uint16_t *)(ptr)))
#define GET_32(ptr)		(*((uint32_t *)(ptr)))
#define GET_64(ptr)		(*((uint64_t *)(ptr)))
#define SET_8(ptr,v)	(*((uint8_t *)(ptr)) = v)
#define SET_16(ptr,v)	(*((uint16_t *)(ptr)) = v)
#define SET_32(ptr,v)	(*((uint32_t *)(ptr)) = v)
#define SET_64(ptr,v)	(*((uint64_t *)(ptr)) = v)

/* wrap terrible C++11 raw string literal syntax */
#define RGX(pattern) std::regex(pattern)

#include "hedley/hedley.h"
#define LIKELY(expr) HEDLEY_LIKELY(expr)
#define UNLIKELY(expr) HEDLEY_UNLIKELY(expr)
