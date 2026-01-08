#include "platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Strsafe.h>

B32 AnanasPlatformGetLine(HeliosAllocator allocator, U8 **out_buffer, UZ *out_count) {
    const UZ buffer_size = 1024;
    char *buffer = HeliosAlloc(allocator, buffer_size);
    HRESULT result = StringCchGetsA(buffer, buffer_size);

    if (FAILED(result)) {
        HeliosFree(allocator, buffer, buffer_size);
        return 0;
    }

    *out_buffer = buffer;
    *out_count = strnlen(buffer, buffer_size);

    return 1;
}

void *AnanasPlatformAllocPages(UZ size) {
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
