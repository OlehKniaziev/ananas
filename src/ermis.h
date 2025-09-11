#ifndef ASTRON_ERMIS_H
#define ASTRON_ERMIS_H

#ifndef ASTRON_HELIOS_H
#    error "ermis.h requires 'helios.h' to be included first"
#endif // ASTRON_HELIOS_H

#define ERMIS_ARRAY_GROW_FACTOR(x) ((((x) + 1) * 3) >> 1)

#define ERMIS_DECL_ARRAY(T, arrname) typedef struct arrname {           \
        HeliosAllocator allocator;                                      \
        T *items;                                                       \
        UZ count;                                                       \
        UZ capacity;                                                    \
    } arrname;                                                          \
                                                                        \
    void arrname##Init(arrname *arr, HeliosAllocator allocator, UZ cap); \
    void arrname##Push(arrname *arr, T item);                           \
                                                                        \
    HELIOS_INLINE T arrname##Pop(arrname *arr) {                        \
        HELIOS_VERIFY(arr->count != 0);                                 \
        return arr->items[--arr->count];                                \
    }                                                                   \
                                                                        \
    HELIOS_INLINE T arrname##At(arrname *arr, UZ idx) {                 \
        HELIOS_VERIFY(idx < arr->count);                                \
        return arr->items[idx];                                         \
    }                                                                   \
                                                                        \
    HELIOS_INLINE T *arrname##AtP(arrname *arr, UZ idx) {               \
        HELIOS_VERIFY(idx < arr->count);                                \
        return &arr->items[idx];                                        \
    }                                                                   \
    \
    HELIOS_INLINE void arrname##Free(arrname *arr) {                    \
        HeliosFree(arr->allocator, arr->items, sizeof(T) * arr->capacity); \
    }

#define ERMIS_IMPL_ARRAY(T, arrname)                                    \
    void arrname##Init(arrname *arr, HeliosAllocator allocator, UZ cap) { \
        arr->allocator = allocator;                                     \
        arr->capacity = cap;                                            \
        arr->items = HeliosAlloc(allocator, sizeof(T) * cap);           \
        arr->count = 0;                                                 \
    }                                                                   \
                                                                        \
    void arrname##Push(arrname *arr, T item) {                          \
        if (arr->count >= arr->capacity) {                              \
            UZ new_capacity = ERMIS_ARRAY_GROW_FACTOR(arr->capacity);   \
            arr->items = HeliosRealloc(arr->allocator, arr->items, sizeof(T) * arr->capacity, sizeof(T) * new_capacity); \
            arr->capacity = new_capacity;                               \
        }                                                               \
                                                                        \
        arr->items[arr->count++] = item;                                \
    }

#define ERMIS_DECL_HASHMAP(K, V, hashmapname) typedef struct hashmapname { \
        K *keys;                                                        \
        V *values;                                                      \
        U8 *meta;                                                       \
        UZ capacity;                                                    \
        UZ count;                                                       \
        HeliosAllocator allocator;                                      \
    } hashmapname;                                                      \
                                                                        \
    void hashmapname##Init(hashmapname *map, HeliosAllocator allocator, UZ cap); \
    B32 hashmapname##Insert(hashmapname *map, K key, V value);          \
    V *hashmapname##FindPtr(hashmapname *map, K key);                   \
                                                                        \
    HELIOS_INLINE B32 hashmapname##Find(hashmapname *map, K key, V *value) { \
        V *found_ptr = hashmapname##FindPtr(map, key);                  \
        if (found_ptr == NULL) {                                        \
            return 0;                                                   \
        } else {                                                        \
            *value = *found_ptr;                                        \
            return 1;                                                   \
        }                                                               \
    }                                                                   \
                                                                        \
    HELIOS_INLINE void hashmapname##Free(hashmapname *map) {            \
        HeliosFree(map->allocator, map->keys, sizeof(K) * map->capacity); \
        HeliosFree(map->allocator, map->values, sizeof(V) * map->capacity); \
        HeliosFree(map->allocator, map->meta, sizeof(map->meta[0]) * map->capacity); \
    }

#define ERMIS_HASHMAP_DEFAULT_CAP (47)

#define ERMIS_HASHMAP_GROW_FACTOR(x) ((x) * 3)

#define ERMIS_HASH_OCCUPIED (1 << 0)

#define ERMIS_HASHMAP_FOREACH(hashmap, keyname, valuename, body)        \
    for (UZ _idx = 0; _idx < (hashmap)->capacity; ++_idx) {             \
        if (((hashmap)->meta[_idx] & ERMIS_HASH_OCCUPIED) == 0) continue; \
        __typeof__((hashmap)->keys[0]) keyname = (hashmap)->keys[_idx]; \
        __typeof__((hashmap)->values[0]) valuename = (hashmap)->values[_idx]; \
        body;                                                           \
    }

#define ERMIS_IMPL_HASHMAP(K, V, hashmapname, eqfunc, hashfunc)         \
    void hashmapname##Init(hashmapname *map, HeliosAllocator allocator, UZ cap) { \
        map->allocator = allocator;                                     \
        map->capacity = cap ? cap : ERMIS_HASHMAP_DEFAULT_CAP;          \
        map->keys = HeliosAlloc(allocator, sizeof(K) * map->capacity);            \
        map->values = HeliosAlloc(allocator, sizeof(V) * map->capacity);          \
        map->meta = HeliosAlloc(allocator, sizeof(map->meta[0]) * map->capacity); \
        map->count = 0;                                                 \
    }                                                                   \
                                                                        \
    int hashmapname##Insert(hashmapname *map, K key, V value) {         \
        UZ load_percentage = map->count * 100 / map->capacity;          \
        if (load_percentage >= 70) {                                    \
            UZ new_cap = ERMIS_HASHMAP_GROW_FACTOR(map->capacity);      \
            hashmapname new_map;                                        \
            hashmapname##Init(&new_map, map->allocator, new_cap);       \
                                                                        \
            ERMIS_HASHMAP_FOREACH(map, key, value, {                    \
                    HELIOS_ASSERT(hashmapname##Insert(&new_map, key, value)); \
                });                                                     \
                                                                        \
            hashmapname##Free(map);                                     \
            *map = new_map;                                             \
        }                                                               \
                                                                        \
        U64 idx = hashfunc(key) % map->capacity;                        \
                                                                        \
        do {                                                            \
            if ((map->meta[idx] & ERMIS_HASH_OCCUPIED) == 0) {          \
                map->keys[idx] = key;                                   \
                map->values[idx] = value;                               \
                map->meta[idx] |= ERMIS_HASH_OCCUPIED;                  \
                ++map->count;                                           \
                return 1;                                               \
            }                                                           \
                                                                        \
            if (eqfunc(map->keys[idx], key)) {                          \
                map->keys[idx] = key;                                   \
                map->values[idx] = value;                               \
                return 0;                                               \
            }                                                           \
                                                                        \
            ++idx;                                                      \
            if (idx >= map->capacity) idx = 0;                          \
        } while (1);                                                    \
    }                                                                   \
                                                                        \
    V *hashmapname##FindPtr(hashmapname *map, K key) {                  \
        U64 idx = hashfunc(key) % map->capacity;                        \
        U64 start_idx = idx;                                            \
                                                                        \
        do {                                                            \
            if ((map->meta[idx] & ERMIS_HASH_OCCUPIED) && eqfunc(key, map->keys[idx])) { \
                return &map->values[idx];                               \
            }                                                           \
                                                                        \
            ++idx;                                                      \
            /* NOTE: using an `if` instead of modulus */                \
            if (idx >= map->capacity) idx = 0;                          \
        } while (idx != start_idx);                                     \
                                                                        \
        return NULL;                                                    \
    }

// Equality and hash functions

HELIOS_INLINE B32 ErmisEqFuncU32(U32 lhs, U32 rhs) { return lhs == rhs; }
HELIOS_INLINE U64 ErmisHashFuncU32(U32 x) { return (U64)x; }
#endif // ASTRON_ERMIS_H
