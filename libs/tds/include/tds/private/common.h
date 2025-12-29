#pragma once
#ifndef _TDS_PRIVATE_COMMON_H_
#define _TDS_PRIVATE_COMMON_H_

#define TDS_JOIN2(a, b) TDS_JOIN2_IMPL(a, b)
#define TDS_JOIN2_IMPL(a, b) a##b

#define TDS_JOIN3(a, b, c) TDS_JOIN3_IMPL(a, b, c)
#define TDS_JOIN3_IMPL(a, b, c) a##b##c

#define TDS_JOIN4(a, b, c, d) TDS_JOIN4_IMPL(a, b, c, d)
#define TDS_JOIN4_IMPL(a, b, c, d) a##b##c##d

#define TDS_JOIN5(a, b, c, d, e) TDS_JOIN5_IMPL(a, b, c, d, e)
#define TDS_JOIN5_IMPL(a, b, c, d, e) a##b##c##d##e

#define TDS_JOIN6(a, b, c, d, e, f) TDS_JOIN6_IMPL(a, b, c, d, e, f)
#define TDS_JOIN6_IMPL(a, b, c, d, e, f) a##b##c##d##e##f

#define TDS_JOIN7(a, b, c, d, e, f, g) TDS_JOIN7_IMPL(a, b, c, d, e, f, g)
#define TDS_JOIN7_IMPL(a, b, c, d, e, f, g) a##b##c##d##e##f##g

#define TDS_DEFAULT_TYPE_W_VALUE(data_structure_name) TDS_JOIN4(TDS_PREFIX, data_structure_name, _, TDS_VALUE_T)
#define TDS_DEFAULT_TYPE_W_KEY_VALUE(data_structure_name) TDS_JOIN6(\
    TDS_PREFIX,\
    data_structure_name,\
    _,\
    TDS_KEY_T,\
    _,\
    TDS_VALUE_T)

#define TDS_IS_SIGNED(T) ((T)-1 < 1)
#define TDS_MAX_VALUE(T) (TDS_IS_SIGNED(T) ? (T)((1ull << ((sizeof(T) << 3) - 1)) - 1) : (T)-1)
#define TDS_COUNTOF(array) (sizeof(array) / sizeof((array)[0]))

#ifndef TDS_PREFIX
#define TDS_PREFIX
#endif

#ifndef TDS_CALLOC
#include <stdlib.h>
#define TDS_CALLOC calloc
#endif

#ifndef TDS_REALLOC
#include <stdlib.h>
#define TDS_REALLOC realloc
#endif

#ifndef TDS_FREE
#include <stdlib.h>
#define TDS_FREE free
#endif

#ifndef TDS_MEMSET
#include <string.h>
#define TDS_MEMSET memset
#endif

#ifndef TDS_MEMMOVE
#include <string.h>
#define TDS_MEMMOVE memmove
#endif

#ifndef TDS_ASSERT
#ifdef NDEBUG
#define TDS_ASSERT(x) ((void)0)
#else
#include <assert.h>
#define TDS_ASSERT assert
#endif
#endif

#ifndef TDS_INITIAL_CAPACITY
#define TDS_INITIAL_CAPACITY 4
#endif

#define TDS_FUNCTION(name) TDS_JOIN3(TDS_TYPE, _, name)
#endif
