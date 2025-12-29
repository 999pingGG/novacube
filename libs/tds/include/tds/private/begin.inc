#if !defined(TDS_DECLARE) && !defined(TDS_IMPLEMENT)
#define TDS_DECLARE
#define TDS_IMPLEMENT
#endif

#ifndef TDS_KEY_T
#define TDS_KEY_T int
#endif

#ifndef TDS_VALUE_T
#define TDS_VALUE_T int
#endif

#ifndef TDS_SIZE_T
#include <stdint.h>
#define TDS_SIZE_T uint32_t
#endif

#ifndef TDS_HASH_KEY
#include <rapidhash.h>
#define TDS_HASH_KEY(key) (rapidhash(&key, sizeof(key)))
#endif
