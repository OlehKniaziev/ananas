#include "common.h"
#include "astron.h"

#define PAGE_SIZE 4096

void AnanasArenaInit(AnanasArena *arena, UZ cap) {
    cap = AnanasAlignForward(cap, PAGE_SIZE);
    arena->data = mmap(NULL, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    arena->capacity = cap;
    arena->offset = 0;
}

void *AnanasArenaPush(AnanasArena *arena, UZ count) {
    count = AnanasAlignForward(count, sizeof(void *));
    UZ bytes_avail = arena->capacity - arena->offset;
    if (bytes_avail < count)
        HELIOS_PANIC_FMT("Tried to allocate %zu bytes on the arena with %zu bytes available",
                         count,
                         bytes_avail);

    void *ptr = arena->data + arena->offset;
    arena->offset += count;
    return ptr;
}

static void *ArenaAllocStub(void *arena, UZ count) {
    return AnanasArenaPush((AnanasArena *)arena, count);
}

static void ArenaFreeStub(void *arena, void *ptr, UZ count) {
    HELIOS_UNUSED(arena);
    HELIOS_UNUSED(ptr);
    HELIOS_UNUSED(count);
}

HeliosAllocator AnanasArenaToHeliosAllocator(AnanasArena *arena) {
    return (HeliosAllocator) {
        .data = arena,
        .vtable = (HeliosAllocatorVTable) {
            .alloc = ArenaAllocStub,
            .free = ArenaFreeStub,
            .realloc = NULL,
        },
    };
}
