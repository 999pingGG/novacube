#include "private/common.h"
#include "private/begin.inc"

#ifndef TDS_TYPE
#define TDS_TYPE TDS_DEFAULT_TYPE_W_KEY_VALUE(hashmap)
#endif

#define TDS_ENTRY_T TDS_JOIN2(TDS_TYPE, _entry)

#ifdef TDS_DECLARE
typedef struct TDS_ENTRY_T {
    uint64_t hash;
    TDS_SIZE_T probe_sequence_length;
    TDS_KEY_T key;
    TDS_VALUE_T value;
    char occupied;
} TDS_ENTRY_T;

typedef struct TDS_TYPE {
    TDS_ENTRY_T* buckets;
    TDS_SIZE_T count;
    TDS_SIZE_T capacity; // Always a prime number.
} TDS_TYPE;

typedef struct TDS_JOIN2(TDS_TYPE, _iter_t) {
    const TDS_TYPE* map;
    TDS_SIZE_T _index;
    TDS_KEY_T key;
    TDS_VALUE_T* value;
} TDS_JOIN2(TDS_TYPE, _iter_t);

TDS_VALUE_T* TDS_FUNCTION(get)(const TDS_TYPE* map, TDS_KEY_T key);
void TDS_FUNCTION(set)(TDS_TYPE* map, TDS_KEY_T key, TDS_VALUE_T value);
TDS_JOIN2(TDS_TYPE, _iter_t) TDS_FUNCTION(iter)(const TDS_TYPE* map);
char TDS_FUNCTION(next)(TDS_JOIN2(TDS_TYPE, _iter_t)* iter);
void TDS_FUNCTION(remove)(TDS_TYPE* map, TDS_KEY_T key);
TDS_SIZE_T TDS_FUNCTION(count)(const TDS_TYPE* map);
void TDS_FUNCTION(clear)(TDS_TYPE* map);
void TDS_FUNCTION(fini)(TDS_TYPE* map);
#endif

#ifdef TDS_IMPLEMENT
TDS_VALUE_T* TDS_FUNCTION(get)(const TDS_TYPE* map, TDS_KEY_T key) {
    if (!map->buckets) {
        return NULL;
    }

    const uint64_t hash = TDS_HASH_KEY(key);
    TDS_SIZE_T index = hash % map->capacity;
    // The "for" instead of a "while" loop is just to guard against infinite loops.
    for (TDS_SIZE_T i = 0; i < map->capacity; i++) {
        TDS_ENTRY_T* cur = map->buckets + index;

        if (!cur->occupied) {
            // Key not found.
            return NULL;
        }

#ifdef TDS_KEY_EQUALS
        if (cur->hash == hash && TDS_KEY_EQUALS(cur->key, key)) {
#else
        if (cur->hash == hash && cur->key == key) {
#endif
            // Key found.
            return &cur->value;
        }

        index = (index + 1) % map->capacity;
    }

    TDS_ASSERT(0);
    return NULL;
}

void TDS_FUNCTION(set)(TDS_TYPE* map, TDS_KEY_T key, TDS_VALUE_T value) {
    if (!map->buckets) {
        // Yeah, we ignore TDS_INITIAL_CAPACITY here because we need the capacity to be a prime number.
        // TODO: Well, use the next prime then.
        map->capacity = 11;
        map->buckets = TDS_CALLOC(map->capacity, sizeof(TDS_ENTRY_T));
    }

    TDS_SIZE_T index;

    // Ensure the map has room for at least one more entry.
    // Check load factor > 0.75 by using integer math instead of floating-point math.
    if ((map->count + 1) * 4 > map->capacity * 3) {
        // Find the smaller prime number that is at least as big as the required capacity.
        static const unsigned long long prime_list[] = {
            2, 3, 5, 11, 17, 37, 67, 131, 257, 521, 1031, 2053, 4099, 8209, 16411, 32771, 65537, 131101, 262147,
            524309, 1048583, 2097169, 4194319, 8388617, 16777259, 33554467, 67108879, 134217757, 268435459, 536870923,
            1073741827, 2147483659, 4294967311, 8589934609, 17179869209, 34359738421, 68719476767, 137438953481,
            274877906951, 549755813911, 1099511627791, 2199023255579, 4398046511119, 8796093022237, 17592186044423,
            35184372088891, 70368744177679, 140737488355333, 281474976710677, 562949953421381, 1125899906842679,
            2251799813685269, 4503599627370517, 9007199254740997, 18014398509482143, 36028797018963971,
            72057594037928017, 144115188075855881, 288230376151711813, 576460752303423619, 1152921504606847009,
            2305843009213693967, 4611686018427388039, 9223372036854775837ull,
        };

        TDS_SIZE_T new_capacity = map->capacity * 2;
        if (new_capacity < map->capacity) {
            // Handle overflow.
            new_capacity = TDS_MAX_VALUE(TDS_SIZE_T);
        } else {
            for (unsigned i = 0; i < TDS_COUNTOF(prime_list); i++) {
                if (prime_list[i] < new_capacity) {
                    continue;
                }

                new_capacity = (TDS_SIZE_T)prime_list[i];
                break;
            }
        }

        // Rehashing.
        TDS_ENTRY_T* new_buckets = TDS_CALLOC(new_capacity, sizeof(TDS_ENTRY_T));
        for (TDS_SIZE_T i = 0; i < map->capacity; i++) {
            TDS_ENTRY_T entry = map->buckets[i];
            if (!entry.occupied) {
                continue;
            }

            entry.probe_sequence_length = 0;

            // Insert entry.
            index = entry.hash % new_capacity;
            while (1) {
                TDS_ENTRY_T* cur = new_buckets + index;
                if (!cur->occupied) {
                    new_buckets[index] = entry;
                    break;
                }

                // Robin Hood: Swap if our probe distance is higher.
                if (cur->probe_sequence_length < entry.probe_sequence_length) {
                    const TDS_ENTRY_T temp = *cur;
                    *cur = entry;
                    entry = temp;
                }
                index = (index + 1) % new_capacity;
                entry.probe_sequence_length++;
                TDS_ASSERT(entry.probe_sequence_length < new_capacity);
            }
        }

        TDS_FREE(map->buckets);
        map->buckets = new_buckets;
        map->capacity = new_capacity;
    }

    // Do the insertion.
    TDS_ENTRY_T new_entry = {
        .hash = TDS_HASH_KEY(key),
        .probe_sequence_length = 0,
        .key = key,
        .value = value,
        .occupied = 1,
    };

    index = new_entry.hash % map->capacity;
    while (1) {
        TDS_ENTRY_T* cur = map->buckets + index;
        if (!cur->occupied) {
            // Key doesn't exist, add it.
            map->buckets[index] = new_entry;
            map->count++;
            return;
        }

#ifdef TDS_KEY_EQUALS
        if (cur->hash == new_entry.hash && TDS_KEY_EQUALS(cur->key, key)) {
#else
        if (cur->hash == new_entry.hash && cur->key == key) {
#endif
            // Key matches, update the value.
            cur->value = value;
            return;
        }

        if (cur->probe_sequence_length < new_entry.probe_sequence_length) {
            // Robin Hood steals from the rich to give to the poor.
            const TDS_ENTRY_T temp = *cur;
            *cur = new_entry;
            new_entry = temp;
        }

        index = (index + 1) % map->capacity;
        new_entry.probe_sequence_length++;
        TDS_ASSERT(new_entry.probe_sequence_length < map->capacity);
    }
}

TDS_JOIN2(TDS_TYPE, _iter_t) TDS_FUNCTION(iter)(const TDS_TYPE* map) {
    return (TDS_JOIN2(TDS_TYPE, _iter_t)) {
        .map = map,
        ._index = 0,
    };
}

char TDS_FUNCTION(next)(TDS_JOIN2(TDS_TYPE, _iter_t)* iter) {
    while (iter->_index < iter->map->capacity) {
        TDS_ENTRY_T* entry = iter->map->buckets + iter->_index++;
        if (entry->occupied) {
            iter->key = entry->key;
            iter->value = &entry->value;
            return 1;
        }
    }

    return 0;
}

void TDS_FUNCTION(remove)(TDS_TYPE* map, TDS_KEY_T key) {
    if (!map->buckets) {
        return;
    }

    const uint64_t hash = TDS_HASH_KEY(key);
    TDS_SIZE_T index = hash % map->capacity;
    for (TDS_SIZE_T i = 0; i < map->capacity; i++) {
        TDS_ENTRY_T* cur = map->buckets + index;

        if (!cur->occupied) {
            // Key not found.
            return;
        }

#ifdef TDS_KEY_EQUALS
        if (cur->hash == hash && TDS_KEY_EQUALS(cur->key, key)) {
#else
        if (cur->hash == hash && cur->key == key) {
#endif
            // Key found, delete it (if applicable) and remove it by marking as unoccupied.
#ifdef TDS_KEY_FINI
            TDS_KEY_FINI((cur->key));
#endif
#ifdef TDS_VALUE_FINI
            TDS_VALUE_FINI((cur->value));
#endif
            cur->occupied = 0;
            map->count--;

            // Now, shift down the chain to maintain the probe sequence.
            TDS_SIZE_T next_index = (index + 1) % map->capacity;
            while (map->buckets[next_index].occupied) {
                TDS_ENTRY_T* next_entry = map->buckets + next_index;

                // If the next entry is where it should be, stop shifting.
                if (next_entry->probe_sequence_length == 0) {
                    break;
                }

                // Move the entry to the previous slot, filling the gap.
                map->buckets[index] = *next_entry;
                map->buckets[index].probe_sequence_length--;
                next_entry->occupied = 0;

                // Update the indices for the next step in the probe chain.
                index = next_index;
                next_index = (index + 1) % map->capacity;
            }

            return;
        }

        index = (index + 1) % map->capacity;
    }

    // This should be unreachable.
    TDS_ASSERT(0);
}

TDS_SIZE_T TDS_FUNCTION(count)(const TDS_TYPE* map) {
    return map->count;
}

void TDS_FUNCTION(clear)(TDS_TYPE* map) {
#if defined(TDS_VALUE_FINI) || defined(TDS_KEY_FINI)
    TDS_JOIN2(TDS_TYPE, _iter_t) it = TDS_FUNCTION(iter)(map);
    while (TDS_FUNCTION(next)(&it)) {
#ifdef TDS_KEY_FINI
        TDS_KEY_FINI((it.key));
#endif
#ifdef TDS_VALUE_FINI
        TDS_VALUE_FINI((it.value));
#endif
    }
#endif
    if (map->buckets) {
        TDS_MEMSET(map->buckets, 0, sizeof(TDS_ENTRY_T) * map->capacity);
    }
    map->count = 0;
}

void TDS_FUNCTION(fini)(TDS_TYPE* map) {
#if defined(TDS_VALUE_FINI) || defined(TDS_KEY_FINI)
    TDS_JOIN2(TDS_TYPE, _iter_t) it = TDS_FUNCTION(iter)(map);
    while (TDS_FUNCTION(next)(&it)) {
#ifdef TDS_KEY_FINI
        TDS_KEY_FINI(it.key);
#endif
#ifdef TDS_VALUE_FINI
        TDS_VALUE_FINI(*it.value);
#endif
    }
#endif
    TDS_FREE(map->buckets);
    *map = (TDS_TYPE){ 0 };
}
#endif

#include "private/end.inc"
