#include "platform.h"

B32 AnanasPlatformGetLine(HeliosAllocator allocator, HeliosStringView *sv) {
    B32 result = 1;

    memset(sv, 0, sizeof(*sv));

    char *temp_buffer = NULL;

    SZ res = getline(&temp_buffer, &sv->count, stdin);
    if (res == -1) {
        result = 0;
        goto defer;
    }

    sv->count = (UZ) res;
    sv->data = HeliosAlloc(allocator, res);
    memcpy(sv->data, temp_buffer, res);

defer:
    free(temp_buffer);
    return result;
}

void *AnanasPlatformAllocPages(UZ size) {
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
}
