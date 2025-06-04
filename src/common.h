#ifndef ANANAS_COMMON_H_
#define ANANAS_COMMON_H_

#include "astron.h"

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

#endif // ANANAS_COMMON_H_
