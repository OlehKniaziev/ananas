#include "platform.h"

B32 AnanasPlatformGetLine(HeliosAllocator allocator, U8 **out_buffer, UZ *out_count) {
    B32 result = 1;

    char *temp_buffer = NULL;

    SZ res = getline(&temp_buffer, out_count, stdin);
    if (res == -1) {
        result = 0;
        goto defer;
    }

    *out_count = res;
    *out_buffer = HeliosAlloc(allocator, res);
    memcpy(*out_buffer, temp_buffer, res);

defer:
    free(temp_buffer);
    return result;
}

void *AnanasPlatformAllocPages(UZ size) {
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
}
