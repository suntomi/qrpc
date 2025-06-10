#pragma once

// 互換性のためのHEDLEYマクロのラッパー
#include "ext/hedley/hedley.h"

// フォーマットセキュリティ警告を無効化するマクロ
#define DISABLE_FORMAT_SECURITY_WARNING_PUSH \
    HEDLEY_DIAGNOSTIC_PUSH \
    HEDLEY_PRAGMA(clang diagnostic ignored "-Wformat-security") \
    HEDLEY_PRAGMA(GCC diagnostic ignored "-Wformat-security")

#define DISABLE_FORMAT_SECURITY_WARNING_POP \
    HEDLEY_DIAGNOSTIC_POP

// その他の一般的な警告無効化マクロ
#define DISABLE_UNUSED_PARAMETER_WARNING_PUSH \
    HEDLEY_DIAGNOSTIC_PUSH \
    HEDLEY_PRAGMA(clang diagnostic ignored "-Wunused-parameter") \
    HEDLEY_PRAGMA(GCC diagnostic ignored "-Wunused-parameter")

#define DISABLE_UNUSED_PARAMETER_WARNING_POP \
    HEDLEY_DIAGNOSTIC_POP

#define DISABLE_CAST_QUAL_WARNING_PUSH \
    HEDLEY_DIAGNOSTIC_PUSH \
    HEDLEY_DIAGNOSTIC_DISABLE_CAST_QUAL

#define DISABLE_CAST_QUAL_WARNING_POP \
    HEDLEY_DIAGNOSTIC_POP

// 非推奨警告を無効化するマクロ
#define DISABLE_DEPRECATED_WARNING_PUSH \
    HEDLEY_DIAGNOSTIC_PUSH \
    HEDLEY_DIAGNOSTIC_DISABLE_DEPRECATED

#define DISABLE_DEPRECATED_WARNING_POP \
    HEDLEY_DIAGNOSTIC_POP
