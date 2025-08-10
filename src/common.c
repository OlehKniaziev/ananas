#include "common.h"
#include "astron.h"

#include <stdarg.h>

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

void AnanasErrorContextMessage(AnanasErrorContext *ctx, U32 row, U32 col, const char *fmt, ...) {
    ctx->ok = 0;

    HeliosAllocator temp_alloc = HeliosGetTempAllocator();

    UZ fmt_buffer_count = strlen(fmt) + ctx->place.count + 50;
    U8 *fmt_buffer = HeliosAlloc(temp_alloc, fmt_buffer_count + 1);
    snprintf((char *)fmt_buffer,
             fmt_buffer_count,
             HELIOS_SV_FMT ":%u:%u: %s",
             HELIOS_SV_ARG(ctx->place),
             row,
             col,
             fmt);

    va_list args;
    va_start(args, fmt);
    vsnprintf((char *)ctx->error_buffer.data, ctx->error_buffer.count, (const char *)fmt_buffer, args);
    va_end(args);
}
