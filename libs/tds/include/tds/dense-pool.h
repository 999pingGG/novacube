#include "private/common.h"
#include "private/begin.inc"

#ifndef TDS_TYPE
#define TDS_TYPE TDS_DEFAULT_TYPE_W_VALUE(dense_pool)
#endif

#ifdef TDS_DECLARE
typedef struct TDS_TYPE {
    TDS_VALUE_T* array;
    TDS_SIZE_T* dense, *sparse, count, capacity;
} TDS_TYPE;

TDS_SIZE_T TDS_FUNCTION(append)(TDS_TYPE* pool, TDS_VALUE_T value);
TDS_SIZE_T TDS_FUNCTION(remove)(TDS_TYPE* pool, TDS_SIZE_T id);
TDS_VALUE_T TDS_FUNCTION(get)(const TDS_TYPE* pool, TDS_SIZE_T id);
TDS_SIZE_T TDS_FUNCTION(count)(const TDS_TYPE* pool);
TDS_VALUE_T* TDS_FUNCTION(first)(const TDS_TYPE* pool);
void TDS_FUNCTION(clear)(TDS_TYPE* pool);
void TDS_FUNCTION(fini)(TDS_TYPE* pool);
#endif

#ifdef TDS_IMPLEMENT
TDS_SIZE_T TDS_FUNCTION(append)(TDS_TYPE* pool, const TDS_VALUE_T value) {
    TDS_ASSERT(pool->count <= pool->capacity);
    // Guard against overflow.
    TDS_ASSERT(pool->count < TDS_MAX_VALUE(TDS_SIZE_T));

    if (pool->count == pool->capacity) {
        TDS_SIZE_T new_capacity = pool->capacity ? pool->capacity * 2 : TDS_INITIAL_CAPACITY;
        if (new_capacity < pool->capacity) {
            // Guard against overflow.
            new_capacity = TDS_MAX_VALUE(TDS_SIZE_T);
        }
        pool->capacity = new_capacity;
        pool->array = TDS_REALLOC(pool->array, new_capacity * sizeof(*pool->array));
        pool->dense = TDS_REALLOC(pool->dense, new_capacity * sizeof(*pool->dense));
        pool->sparse = TDS_REALLOC(pool->sparse, new_capacity * sizeof(*pool->sparse));
        const TDS_SIZE_T new_elements = new_capacity - pool->count;
        for (TDS_SIZE_T i = 0; i < new_elements; i++) {
            pool->dense[pool->count + i] = TDS_MAX_VALUE(TDS_SIZE_T);
        }
    }

    const TDS_SIZE_T new_sparse_id = pool->dense[pool->count] == TDS_MAX_VALUE(TDS_SIZE_T)
        ? pool->count
        : pool->dense[pool->count];
    pool->sparse[new_sparse_id] = pool->count;
    pool->dense[pool->count] = new_sparse_id;

    pool->array[pool->count] = value;
    pool->count++;
    return new_sparse_id;
}

TDS_SIZE_T TDS_FUNCTION(remove)(TDS_TYPE* pool, const TDS_SIZE_T id) {
    TDS_ASSERT(id < pool->capacity);
    TDS_ASSERT(pool->sparse[id] < pool->count);
    TDS_ASSERT(pool->dense[pool->sparse[id]] == id);

#ifdef TDS_VALUE_FINI
    TDS_VALUE_FINI(vec->array[index]);
#endif

    const TDS_SIZE_T dense_index = pool->sparse[id];
    if (dense_index == pool->count - 1) {
        pool->dense[dense_index] =
            // Check the next free ID, while avoid reading one past the end.
            pool->count == pool->capacity || pool->dense[pool->count] == TDS_MAX_VALUE(TDS_SIZE_T)
                ? TDS_MAX_VALUE(TDS_SIZE_T)
                : dense_index;
    } else {
        // Move the last element into the just freed position.
        pool->array[dense_index] = pool->array[pool->count - 1];

        pool->sparse[pool->count - 1] = dense_index;
        pool->dense[dense_index] = pool->count - 1;

        // Free index bookkeeping.
        pool->dense[pool->count - 1] = id;
    }
    pool->count--;
    return dense_index;
}

TDS_VALUE_T TDS_FUNCTION(get)(const TDS_TYPE* pool, const TDS_SIZE_T id) {
    TDS_ASSERT(id < pool->capacity);
    TDS_ASSERT(pool->sparse[id] < pool->count);
    TDS_ASSERT(pool->dense[pool->sparse[id]] == id);

    return pool->array[pool->sparse[id]];
}

TDS_SIZE_T TDS_FUNCTION(count)(const TDS_TYPE* pool) {
    return pool->count;
}

TDS_VALUE_T* TDS_FUNCTION(first)(const TDS_TYPE* pool) {
    return pool->array;
}

void TDS_FUNCTION(clear)(TDS_TYPE* pool) {
#ifdef TDS_VALUE_FINI
    for (TDS_SIZE_T i = 0; i < pool->count; i++) {
        TDS_VALUE_FINI(vec->array[i]);
    }
#endif
    for (TDS_SIZE_T i = 0; i < pool->capacity; i++) {
        pool->dense[i] = TDS_MAX_VALUE(TDS_SIZE_T);
    }
    pool->count = 0;
}

void TDS_FUNCTION(fini)(TDS_TYPE* pool) {
#ifdef TDS_VALUE_FINI
    for (TDS_SIZE_T i = 0; i < pool->count; i++) {
        TDS_VALUE_FINI(vec->array[i]);
    }
#endif
    TDS_FREE(pool->array);
    TDS_FREE(pool->dense);
    TDS_FREE(pool->sparse);
    *pool= (TDS_TYPE){ 0 };
}
#endif

#include "private/end.inc"
