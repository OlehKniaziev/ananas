#ifndef ANANAS_COMMON_H_
#define ANANAS_COMMON_H_

#include "astron.h"
#include "lexer.h"

typedef struct {
    HeliosStringView place;
    U32 row;
    U32 col;
} AnanasLocation;

typedef struct {
    B32 ok;
    HeliosStringView place;
    HeliosStringView error_buffer;
} AnanasErrorContext;

void AnanasErrorContextMessage(AnanasErrorContext *ctx, U32 row, U32 col, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

typedef struct {
    U8 *data;
    UZ capacity;
    UZ offset;
} AnanasArena;

void AnanasArenaInit(AnanasArena *arena, UZ capacity);

void *AnanasArenaPush(AnanasArena *arena, UZ count);

HeliosAllocator AnanasArenaToHeliosAllocator(AnanasArena *arena);

#define ANANAS_ARENA_PUSH_ZERO(arena, size) (memset(AnanasArenaPush((arena), (size)), 0, (size)))
#define ANANAS_ARENA_STRUCT_ZERO(arena, type) ((type *)ANANAS_ARENA_PUSH_ZERO(arena, sizeof(type)))

static inline UZ AnanasAlignForward(UZ x, UZ align) {
    return x + (align - ((x & (align - 1)) & (align - 1)));
}

static inline U64 AnanasFnv1Hash(HeliosStringView sv) {
    const U64 fnv_offset_basis = 0xCBF29CE484222325;
    const U64 fnv_prime = 0x100000001B3;

    U64 hash = fnv_offset_basis;

    for (UZ i = 0; i < sv.count; ++i) {
        hash *= fnv_prime;

        U8 byte = sv.data[i];
        hash ^= (U64)byte;
    }

    return hash;
}

#endif // ANANAS_COMMON_H_
