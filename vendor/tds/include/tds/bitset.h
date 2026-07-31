#include "private/common.h"
#include "private/begin.inc"

#ifndef TDS_BIT_COUNT
#error "TDS_BIT_COUNT must be defined before including bitset.h"
#endif

#if TDS_BIT_COUNT <= 0
#error "TDS_BIT_COUNT must be greater than zero."
#endif

#ifndef TDS_WORD_T
#include <stdint.h>
#define TDS_WORD_T uint64_t
#endif

#ifndef TDS_TYPE
#define TDS_TYPE TDS_JOIN3(bitset_, TDS_BIT_COUNT, _t)
#endif

#define TDS_WORD_BIT_COUNT (sizeof(TDS_WORD_T) * CHAR_BIT)
#define TDS_WORD_COUNT ((TDS_BIT_COUNT + TDS_WORD_BIT_COUNT - 1) / TDS_WORD_BIT_COUNT)

#ifdef TDS_DECLARE
typedef struct TDS_TYPE {
    TDS_WORD_T words[TDS_WORD_COUNT];
} TDS_TYPE;

int TDS_FUNCTION(get)(const TDS_TYPE* bitset, TDS_SIZE_T index);
void TDS_FUNCTION(set)(TDS_TYPE* bitset, TDS_SIZE_T index);
void TDS_FUNCTION(clear)(TDS_TYPE* bitset, TDS_SIZE_T index);
void TDS_FUNCTION(set_all)(TDS_TYPE* bitset);
void TDS_FUNCTION(clear_all)(TDS_TYPE* bitset);
int TDS_FUNCTION(any)(const TDS_TYPE* bitset);
int TDS_FUNCTION(none)(const TDS_TYPE* bitset);
#endif

#ifdef TDS_IMPLEMENT
int TDS_FUNCTION(get)(const TDS_TYPE* bitset, const TDS_SIZE_T index) {
    TDS_ASSERT(index < TDS_BIT_COUNT);
    return (bitset->words[index / TDS_WORD_BIT_COUNT] & ((TDS_WORD_T)1 << (index % TDS_WORD_BIT_COUNT))) != 0;
}

void TDS_FUNCTION(set)(TDS_TYPE* bitset, const TDS_SIZE_T index) {
    TDS_ASSERT(index < TDS_BIT_COUNT);
    bitset->words[index / TDS_WORD_BIT_COUNT] |= (TDS_WORD_T)1 << (index % TDS_WORD_BIT_COUNT);
}

void TDS_FUNCTION(clear)(TDS_TYPE* bitset, const TDS_SIZE_T index) {
    TDS_ASSERT(index < TDS_BIT_COUNT);
    bitset->words[index / TDS_WORD_BIT_COUNT] &= ~((TDS_WORD_T)1 << (index % TDS_WORD_BIT_COUNT));
}

void TDS_FUNCTION(set_all)(TDS_TYPE* bitset) {
    TDS_MEMSET(bitset->words, 0xff, sizeof(bitset->words));

    // Easily optimized out by compilers in applicable cases.
    const TDS_SIZE_T used_bits_in_last_word = TDS_BIT_COUNT % TDS_WORD_BIT_COUNT;
    if (used_bits_in_last_word != 0) {
        bitset->words[TDS_WORD_COUNT - 1] &=
            ((TDS_WORD_T)1 << used_bits_in_last_word) - 1;
    }
}

void TDS_FUNCTION(clear_all)(TDS_TYPE* bitset) {
    TDS_MEMSET(bitset->words, 0, sizeof(bitset->words));
}

int TDS_FUNCTION(any)(const TDS_TYPE* bitset) {
    for (TDS_SIZE_T i = 0; i < TDS_WORD_COUNT; i++) {
        if (bitset->words[i] != 0) {
            return 1;
        }
    }
    return 0;
}

int TDS_FUNCTION(none)(const TDS_TYPE* bitset) {
    return !TDS_FUNCTION(any)(bitset);
}
#endif

#include "private/end.inc"

#undef TDS_BIT_COUNT
#undef TDS_WORD_T
#undef TDS_WORD_BIT_COUNT
#undef TDS_WORD_COUNT
