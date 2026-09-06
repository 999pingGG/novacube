#pragma once
#ifndef NOVACUBE_INTRINSICS_H_
#define NOVACUBE_INTRINSICS_H_

#include <stdint.h>

#if defined(_M_IX86) || defined(_M_X64)
#include <intrin.h>
#define NC__X86_INTRINSICS
#elif defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#define NC__X86_INTRINSICS
#endif

static inline uint16_t nc__u16_trailing_zeroes(const uint16_t x) {
#ifdef _MSC_VER
    return x == 0 ? 16 : _BitScanForward(x);
#else
    #if defined(NC__X86_INTRINSICS) && defined(__BMI__)
        return _tzcnt_u16(x);
    #else
        return x == 0 ? 16 : (uint16_t)__builtin_ctz((unsigned int)x);
    #endif
#endif
}

static inline uint16_t nc__u16_trailing_ones(const uint16_t x) {
#ifdef _MSC_VER
    return x == UINT16_MAX ? 16 : _BitScanForward((uint16_t)~x);
#else
    #if defined(NC__X86_INTRINSICS) && defined(__BMI__)
        return _tzcnt_u16((uint16_t)~x);
    #else
        return x == UINT16_MAX ? 16 : (uint16_t)__builtin_ctz((unsigned int)(uint16_t)~x);
    #endif
#endif
}

static inline uint32_t nc__u32_trailing_zeroes(const uint32_t x) {
#if defined(NC__X86_INTRINSICS) && defined(__BMI__)
    return _tzcnt_u32(x);
#else
    return x == 0 ? 32 : (uint32_t)__builtin_ctz(x);
#endif
}

static inline uint32_t nc__u32_trailing_ones(const uint32_t x) {
#if defined(NC__X86_INTRINSICS) && defined(__BMI__)
    return _tzcnt_u32(~x);
#else
    return x == UINT32_MAX ? 32 : (uint32_t)__builtin_ctz(~x);
#endif
}

#endif
