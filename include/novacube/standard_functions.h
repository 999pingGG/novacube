#pragma once
#ifndef NOVACUBE_STANDARD_FUNCTIONS_H_
#define NOVACUBE_STANDARD_FUNCTIONS_H_
// This is necessary because on Android `assert(x)` apparently works even in release mode.
#ifdef NDEBUG
#define NC_ASSERT(x) ((void)0)
#else
#include <assert.h>
#define NC_ASSERT(x) assert(x)
#endif

#include <string.h>
#define NC_MEMSET(buffer, character, n) memset(buffer, character, n)
#define NC_MEMCPY(destination, source, n) memcpy(destination, source, n)

#include <stdlib.h>
#define NC_STRTOD(string, endptr) strtod(string, endptr)
#define NC_DTOA() dtoa()

#include <stdio.h>
#define NC_VSNPRINTF(buffer, buffer_size, format, arguments) vsnprintf(buffer, buffer_size, format, arguments)

#endif
