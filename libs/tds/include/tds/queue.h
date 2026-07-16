#include "private/common.h"
#include "private/begin.inc"

#ifndef TDS_TYPE
#define TDS_TYPE TDS_DEFAULT_TYPE_W_VALUE(queue)
#endif

#ifdef TDS_DECLARE
typedef struct TDS_TYPE {
    TDS_VALUE_T* array;
    TDS_SIZE_T head, count, capacity;
} TDS_TYPE;

void TDS_FUNCTION(push)(TDS_TYPE* queue, TDS_VALUE_T value);
TDS_VALUE_T TDS_FUNCTION(pop)(TDS_TYPE* queue);
TDS_VALUE_T* TDS_FUNCTION(front)(const TDS_TYPE* queue);
void TDS_FUNCTION(reserve)(TDS_TYPE* queue, TDS_SIZE_T capacity);
TDS_SIZE_T TDS_FUNCTION(count)(const TDS_TYPE* queue);
void TDS_FUNCTION(clear)(TDS_TYPE* queue);
void TDS_FUNCTION(reclaim)(TDS_TYPE* queue);
void TDS_FUNCTION(fini)(TDS_TYPE* queue);
#endif

#ifdef TDS_IMPLEMENT
static void TDS_FUNCTION(_reallocate)(TDS_TYPE* queue, const TDS_SIZE_T capacity) {
    TDS_ASSERT(queue->count <= queue->capacity);
    TDS_ASSERT(queue->count <= capacity);

    TDS_VALUE_T* array = TDS_CALLOC(capacity, sizeof(TDS_VALUE_T));
    if (queue->count) {
        const TDS_SIZE_T first_count = queue->count < queue->capacity - queue->head
                ? queue->count
                : queue->capacity - queue->head;
        TDS_MEMCPY(array, queue->array + queue->head, (size_t)first_count * sizeof(TDS_VALUE_T));
        if (first_count < queue->count) {
            TDS_MEMCPY(
                    array + first_count,
                    queue->array,
                    (size_t)(queue->count - first_count) * sizeof(TDS_VALUE_T));
        }
    }

    TDS_FREE(queue->array);
    queue->array = array;
    queue->head = 0;
    queue->capacity = capacity;
}

void TDS_FUNCTION(push)(TDS_TYPE* queue, const TDS_VALUE_T value) {
    TDS_ASSERT(queue->count <= queue->capacity);
    TDS_ASSERT(queue->count < TDS_MAX_VALUE(TDS_SIZE_T));

    if (queue->count == queue->capacity) {
        const TDS_SIZE_T capacity = queue->capacity == 0
                ? TDS_INITIAL_CAPACITY
                : (queue->capacity > TDS_MAX_VALUE(TDS_SIZE_T) / 2 ? TDS_MAX_VALUE(TDS_SIZE_T) : queue->capacity * 2);
        TDS_FUNCTION(reserve)(queue, capacity);
    }

    const TDS_SIZE_T remaining = queue->capacity - queue->head;
    const TDS_SIZE_T tail = queue->count >= remaining
            ? queue->count - remaining
            : queue->head + queue->count;
    queue->array[tail] = value;
    queue->count++;
}

TDS_VALUE_T TDS_FUNCTION(pop)(TDS_TYPE* queue) {
    TDS_ASSERT(queue->count > 0);

    const TDS_VALUE_T value = queue->array[queue->head];
    queue->count--;
    if (queue->count == 0) {
        queue->head = 0;
    } else {
        queue->head = (queue->head + 1) % queue->capacity;
    }

    return value;
}

TDS_VALUE_T* TDS_FUNCTION(front)(const TDS_TYPE* queue) {
    return queue->count ? queue->array + queue->head : NULL;
}

void TDS_FUNCTION(reserve)(TDS_TYPE* queue, const TDS_SIZE_T capacity) {
    TDS_ASSERT(queue->count <= queue->capacity);
    if (capacity > queue->capacity) {
        TDS_FUNCTION(_reallocate)(queue, capacity);
    }
}

TDS_SIZE_T TDS_FUNCTION(count)(const TDS_TYPE* queue) {
    return queue->count;
}

void TDS_FUNCTION(clear)(TDS_TYPE* queue) {
#ifdef TDS_VALUE_FINI
    for (TDS_SIZE_T i = 0; i < queue->count; i++) {
        const TDS_SIZE_T remaining = queue->capacity - queue->head;
        const TDS_SIZE_T index = i >= remaining ? i - remaining : queue->head + i;
        TDS_VALUE_FINI(queue->array[index]);
    }
#endif
    queue->head = 0;
    queue->count = 0;
}

void TDS_FUNCTION(reclaim)(TDS_TYPE* queue) {
    TDS_ASSERT(queue->count <= queue->capacity);
    if (queue->count == queue->capacity) {
        return;
    }
    if (queue->count == 0) {
        TDS_FREE(queue->array);
        *queue = (TDS_TYPE){ 0 };
        return;
    }
    TDS_FUNCTION(_reallocate)(queue, queue->count);
}

void TDS_FUNCTION(fini)(TDS_TYPE* queue) {
    TDS_FUNCTION(clear)(queue);
    TDS_FREE(queue->array);
    *queue = (TDS_TYPE){ 0 };
}
#endif

#include "private/end.inc"
