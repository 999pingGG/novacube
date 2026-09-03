#pragma once
#ifndef NOVACUBE_MACROS_H_
#define NOVACUBE_MACROS_H_

#if defined(__clang__)
#define NC_IGNORE_ALL_WARNINGS_BEGIN _Pragma("clang diagnostic push") \
_Pragma("clang diagnostic ignored \"-Weverything\"")
#define NC_IGNORE_ALL_WARNINGS_END   _Pragma("clang diagnostic pop")
#elif defined(_MSC_VER)
#define NC_IGNORE_ALL_WARNINGS_BEGIN __pragma(warning(push, 0)) __pragma(warning(disable: 4701))
#define NC_IGNORE_ALL_WARNINGS_END   __pragma(warning(pop))
#elif defined(__GNUC__)
#define NC_IGNORE_ALL_WARNINGS_BEGIN _Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wmissing-braces\"")
_Pragma("GCC diagnostic ignored \"-Wempty-body\"")
_Pragma("GCC diagnostic ignored \"-Wpedantic\"")
_Pragma("GCC diagnostic ignored \"-Wunused-variable\"")
_Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")
_Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
_Pragma("GCC diagnostic ignored \"-Wsign-compare\"")
_Pragma("GCC diagnostic ignored \"-Waggressive-loop-optimizations\"")
_Pragma("GCC diagnostic ignored \"-Wunknown-pragmas\"")
#define NC_IGNORE_ALL_WARNINGS_END   _Pragma("GCC diagnostic pop")
#else
#define NC_IGNORE_ALL_WARNINGS_BEGIN
#define NC_IGNORE_ALL_WARNINGS_END
#endif

#ifdef NDEBUG
#define NC_BUILD_TYPE "Release"
#else
#define NC_BUILD_TYPE "Debug"
#endif

#define NC_COUNTOF(array) (sizeof(array) / sizeof(*array))
#define NC__CONCAT_IMPL(a, b) a ## b
#define NC__CONCAT(a, b) NC__CONCAT_IMPL(a, b)

#endif
