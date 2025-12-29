#include "private/common.h"
#include "private/begin.inc"

#ifndef TDS_TYPE
#define TDS_TYPE TDS_DEFAULT_TYPE_W_VALUE(vec)
#endif

#ifdef TDS_DECLARE
typedef struct TDS_TYPE {
    TDS_VALUE_T* array;
    TDS_SIZE_T count, capacity;
} TDS_TYPE;

void TDS_FUNCTION(append)(TDS_TYPE* vec, TDS_VALUE_T value);
void TDS_FUNCTION(remove)(TDS_TYPE* vec, TDS_SIZE_T index);
TDS_VALUE_T TDS_FUNCTION(get)(const TDS_TYPE* vec, TDS_SIZE_T index);
TDS_SIZE_T TDS_FUNCTION(count)(const TDS_TYPE* vec);
TDS_VALUE_T* TDS_FUNCTION(first)(const TDS_TYPE* vec);
void TDS_FUNCTION(clear)(TDS_TYPE* vec);
void TDS_FUNCTION(fini)(TDS_TYPE* vec);
#endif

#ifdef TDS_IMPLEMENT
void TDS_FUNCTION(append)(TDS_TYPE* vec, const TDS_VALUE_T value) {
    TDS_ASSERT(vec->count <= vec->capacity);
    // Guard against overflow.
    TDS_ASSERT(vec->count < TDS_MAX_VALUE(TDS_SIZE_T));

    if (vec->count == vec->capacity) {
        TDS_SIZE_T new_capacity = vec->capacity ? vec->capacity * 2 : TDS_INITIAL_CAPACITY;
        if (new_capacity < vec->capacity) {
            // Guard against overflow.
            new_capacity = TDS_MAX_VALUE(TDS_SIZE_T);
        }
        vec->capacity = new_capacity;
        vec->array = TDS_REALLOC(vec->array, sizeof(TDS_VALUE_T) * vec->capacity);
    }

    vec->array[vec->count] = value;
    vec->count++;
}

void TDS_FUNCTION(remove)(TDS_TYPE* vec, const TDS_SIZE_T index) {
    TDS_ASSERT(index < vec->count);

#ifdef TDS_VALUE_FINI
    TDS_VALUE_FINI(vec->array[index]);
#endif

    if (index < vec->count - 1) {
        // Shift elements to the left
        TDS_MEMMOVE(&vec->array[index], &vec->array[index + 1], (size_t)(vec->count - index - 1) * sizeof(TDS_VALUE_T));
    }
    vec->count--;
}

TDS_VALUE_T TDS_FUNCTION(get)(const TDS_TYPE* vec, const TDS_SIZE_T index) {
    TDS_ASSERT(index < vec->count);
    return vec->array[index];
}

TDS_SIZE_T TDS_FUNCTION(count)(const TDS_TYPE* vec) {
    return vec->count;
}

TDS_VALUE_T* TDS_FUNCTION(first)(const TDS_TYPE* vec) {
    return vec->array;
}

void TDS_FUNCTION(clear)(TDS_TYPE* vec) {
#ifdef TDS_VALUE_FINI
    for (TDS_SIZE_T i = 0; i < vec->count; i++) {
        TDS_VALUE_FINI(vec->array[i]);
    }
#endif
    vec->count = 0;
}

void TDS_FUNCTION(fini)(TDS_TYPE* vec) {
#ifdef TDS_VALUE_FINI
    for (TDS_SIZE_T i = 0; i < vec->count; i++) {
        TDS_VALUE_FINI(vec->array[i]);
    }
#endif
    TDS_FREE(vec->array);
    *vec = (TDS_TYPE){ 0 };
}
#endif

#include "private/end.inc"
