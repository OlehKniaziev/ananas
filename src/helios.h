#ifndef ASTRON_HELIOS_H
#define ASTRON_HELIOS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifndef HELIOS_STRIP_ASSERTS
#    define HELIOS_ASSERT(cond) do {                                        \
            if (!(cond)) {                                                  \
                fprintf(stderr, "%s:%d: ASSERTION FAILED: %s\n", __FILE__, __LINE__, #cond); \
                abort();                                                    \
            }                                                               \
        } while (0)

#else
#    define HELIOS_ASSERT(cond)
#endif // HELIOS_STRIP_ASSERTS

#define HELIOS_VERIFY(cond) do {                                        \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: VERIFICATION FAILED: %s\n", __FILE__, __LINE__, #cond); \
            abort();                                                    \
        }                                                               \
    } while (0)

#define HELIOS_UNUSED(x) (void)(x)

#define HELIOS_TODO() do {                                      \
        fprintf(stderr, "%s:%d: TODO\n", __FILE__, __LINE__);     \
        abort();                                                \
    } while (0)

#define HELIOS_PANIC(msg) do {                                      \
        fprintf(stderr, "%s:%d: PROGRAM PANICKED: %s\n", __FILE__, __LINE__, msg);     \
        abort();                                                \
    } while (0)

#define HELIOS_PANIC_FMT(fmt, ...) do {                                     \
        fprintf(stderr, "%s:%d: PROGRAM PANICKED: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__);     \
        abort();                                                \
    } while (0)

#define HELIOS_UNREACHABLE() do {                                      \
        fprintf(stderr, "%s:%d: ENCOUNTERED UNREACHABLE CODE\n", __FILE__, __LINE__);     \
        abort();                                                \
    } while (0)

#ifdef _MSC_VER
#    define HELIOS_INLINE __forceinline
#else
#    define HELIOS_INLINE __attribute__((always_inline)) inline
#endif // compiler check

#define HELIOS_PAGE_SIZE (1024 * 4)

#ifdef _WIN32
#    define HELIOS_PLATFORM_WINDOWS
#    define HELIOS_PAGE_ALIGNMENT (1024 * 64)

#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    define HELIOS_PLATFORM_POSIX
#    define HELIOS_PAGE_ALIGNMENT (1024 * 4)

#    include <fcntl.h>
#    include <unistd.h>
#    include <sys/stat.h>
#    include <sys/mman.h>
#endif // _WIN32

#if defined(__clang__)
#    define HELIOS_COMPILER_CLANG
#elif defined(__GNUC__)
#    define HELIOS_COMPILER_GCC
#elif defined(_MSVC_VER)
#    define HELIOS_COMPILER_MSVC
#endif

#define HELIOS_INTERNAL static

#ifdef HELIOS_STATIC
#define HELIOS_DEF HELIOS_INTERNAL
#else
#define HELIOS_DEF extern
#endif // HELIOS_STATIC

#if SIZE_MAX == UINT32_MAX
#    define HELIOS_BITS_32
#elif SIZE_MAX == UINT64_MAX
#    define HELIOS_BITS_64
#else
#    error "Could not determine architecture bit size"
#endif // word size check

typedef uint8_t  U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef int8_t  S8;
typedef int16_t S16;
typedef int32_t S32;
typedef int64_t S64;

typedef S8  B8;
typedef S16 B16;
typedef S32 B32;
typedef S64 B64;

typedef float  F32;
typedef double F64;

#if defined(HELIOS_BITS_32)
    typedef uint32_t UZ;
    typedef int32_t  SZ;
#elif defined(HELIOS_BITS_64)
    typedef uint64_t UZ;
    typedef int64_t  SZ;
#endif // bit size check

typedef struct HeliosAllocatorVTable {
    void *(*alloc)(void*, UZ);              // required
    void  (*free)(void*, void*, UZ);        // required
    void *(*realloc)(void*, void*, UZ, UZ); // optional
} HeliosAllocatorVTable;

typedef struct HeliosAllocator {
    HeliosAllocatorVTable vtable;
    void *data;
} HeliosAllocator;

void *HeliosRawAlloc(UZ);

HELIOS_INLINE void *HeliosAlloc(HeliosAllocator allocator, UZ size) {
    return allocator.vtable.alloc(allocator.data, size);
}

HELIOS_INLINE void HeliosFree(HeliosAllocator allocator, void *ptr, UZ size) {
    allocator.vtable.free(allocator.data, ptr, size);
}

HELIOS_INLINE void *HeliosRealloc(HeliosAllocator allocator, void *old_ptr, UZ old_size, UZ size) {
    if (allocator.vtable.realloc != NULL)
        return allocator.vtable.realloc(allocator.data, old_ptr, old_size, size);

    void *new_ptr = HeliosAlloc(allocator, size);
    memcpy(new_ptr, old_ptr, old_size);
    HeliosFree(allocator, old_ptr, old_size);
    return new_ptr;
}

HeliosAllocator HeliosNewMallocAllocator(void);

HELIOS_INLINE UZ HeliosRoundUp(UZ size, UZ align) {
    return size + ((align - (size & (align - 1))) & (align - 1));
}

HELIOS_INLINE UZ HeliosRoundDown(UZ size, UZ align) {
    return size - (size & (align - 1));
}

HeliosAllocator HeliosGetTempAllocator(void);

typedef struct HeliosString8 {
    U8 *data;
    UZ count;
    UZ capacity;
    HeliosAllocator allocator;
} HeliosString8;

typedef struct HeliosStringView {
    const U8 *data;
    UZ count;
} HeliosStringView;

#define HELIOS_SV_FMT "%.*s"
#define HELIOS_SV_ARG(sv) (int)(sv).count, (const char*)((sv).data)
#define HELIOS_SV_LIT(cstr) ((HeliosStringView) {.data = (U8 *)(cstr), .count = strlen((cstr))})

HELIOS_INLINE HeliosStringView HeliosStringViewClone(HeliosAllocator allocator, HeliosStringView sv) {
    U8 *data = (U8 *)HeliosAlloc(allocator, sv.count);
    memcpy(data, sv.data, sv.count);
    return (HeliosStringView) { .data = data, .count = sv.count };
}

HELIOS_INLINE char *HeliosStringViewCloneToCStr(HeliosAllocator allocator, HeliosStringView sv) {
    char *data = (char *)HeliosAlloc(allocator, sv.count + 1);
    memcpy(data, sv.data, sv.count);
    return data;
}

HELIOS_INLINE B32 HeliosStringViewStartsWithSV(HeliosStringView sv, HeliosStringView prefix) {
    if (prefix.count > sv.count) return 0;

    for (UZ i = 0; i < prefix.count; ++i) {
        if (sv.data[i] != prefix.data[i]) return 0;
    }

    return 1;
}

HELIOS_INLINE B32 HeliosStringViewStartsWith(HeliosStringView sv, const char *prefix) {
    UZ prefix_count = strlen(prefix);
    HeliosStringView prefix_sv = { .data = (const U8 *)prefix, .count = prefix_count };
    return HeliosStringViewStartsWithSV(sv, prefix_sv);
}

HELIOS_INLINE B32 HeliosStringViewEqual(HeliosStringView lhs, HeliosStringView rhs) {
    if (lhs.count != rhs.count) return 0;

    for (UZ i = 0; i < lhs.count; ++i) {
        if (lhs.data[i] != rhs.data[i]) return 0;
    }

    return 1;
}

HELIOS_INLINE B32 HeliosStringViewEqualCStr(HeliosStringView sv, const char *cstr) {
    UZ cstr_len = strlen(cstr);
    if (cstr_len != sv.count) return 0;

    for (UZ i = 0; i < sv.count; ++i) {
        if (sv.data[i] != (U8)cstr[i]) return 0;
    }

    return 1;
}

HELIOS_INLINE HeliosStringView HeliosString8View(HeliosString8 s) {
    return (HeliosStringView) {
        .data = s.data,
        .count = s.count,
    };
}

HELIOS_INLINE HeliosString8 HeliosString8FromSV(HeliosAllocator allocator, HeliosStringView sv) {
    U8 *data = (U8 *)HeliosAlloc(allocator, sv.count + 1);
    memcpy(data, sv.data, sv.count);
    data[sv.count + 1] = '\0';

    return (HeliosString8) {
        .data = data,
        .count = sv.count,
        .capacity = sv.count,
        .allocator = allocator,
    };
}

HELIOS_DEF B32 HeliosParseS64(HeliosStringView sv, U8 base, S64 *out);
HELIOS_DEF B32 HeliosParseS64DetectBase(HeliosStringView sv, S64 *out);
HELIOS_DEF B32 HeliosParseF64(HeliosStringView, F64 *);

typedef U32 HeliosChar;

HELIOS_INLINE B32 HeliosCharIsDigit(HeliosChar c) {
    return c >= '0' && c <= '9';
}

HELIOS_INLINE B32 HeliosCharIsAlpha(HeliosChar c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

HELIOS_INLINE B32 HeliosCharIsAlnum(HeliosChar c) {
    return HeliosCharIsDigit(c) || HeliosCharIsAlpha(c);
}

HELIOS_DEF HeliosString8 HeliosString8FromCStr(HeliosAllocator, const char *);
HELIOS_DEF B32 HeliosString8FromCStrChecked(HeliosAllocator, const char *, HeliosString8 *);

typedef struct HeliosString8Stream {
    const U8 *data;
    UZ count;
    UZ byte_offset;
    UZ char_offset;
    S8 last_char_size;
} HeliosString8Stream;

HELIOS_DEF void HeliosString8StreamInit(HeliosString8Stream *, const U8 *, UZ);
HELIOS_DEF B32 HeliosString8StreamCur(HeliosString8Stream *, HeliosChar *);
HELIOS_DEF B32 HeliosString8StreamNext(HeliosString8Stream *, HeliosChar *);
HELIOS_DEF void HeliosString8StreamRetreat(HeliosString8Stream *);

HELIOS_INLINE HeliosString8 HeliosString8FromStringView(HeliosAllocator allocator, HeliosStringView sv) {
    UZ s_count = sv.count;

    U8 *s_data = (U8 *)HeliosAlloc(allocator, s_count);
    memcpy(s_data, sv.data, s_count);
    s_data[s_count] = '\0';

    return (HeliosString8) {
        .data = s_data,
        .count = s_count,
        .capacity = s_count,
        .allocator = allocator,
    };
}

HELIOS_DEF HeliosStringView HeliosReadEntireFile(HeliosAllocator, HeliosStringView);

#ifdef __cplusplus
    }
#endif // __cplusplus

#ifdef ASTRON_HELIOS_IMPLEMENTATION

HELIOS_DEF B32 _HeliosParseS64Hex(HeliosStringView source, S64 *out) {
    if (source.count == 0) return 0;

    S64 result = 0;
    UZ coef = 1;

    for (SZ i = source.count - 1; i >= 0; --i) {
        U8 c = source.data[i];

        U8 digit;
        if (isdigit(c))                digit = c - '0';
        else if (c >= 'A' && c <= 'F') digit = c - 55;
        else if (c >= 'a' && c <= 'f') digit = c - 87;
        else return 0;

        result += digit * coef;
        coef *= 16;
    }

    *out = result;

    return 1;
}

HELIOS_DEF B32 _HeliosParseS64Generic(HeliosStringView source, U8 base, S64 *out) {
    HELIOS_ASSERT(base != 16);

    if (source.count == 0) return 0;

    S64 result = 0;
    UZ coef = 1;

    for (UZ i = source.count - 1; i > 0; --i) {
        U8 c = source.data[i];
        if (!isdigit(c)) return 0;

        U8 digit = c - '0';
        if (digit >= base) return 0;

        result += digit * coef;
        coef *= base;
    }

    if (isdigit(source.data[0])) {
        U8 digit = source.data[0] - '0';
        if (digit >= base) return 0;
        result += digit * coef;
    } else {
        if (base != 10) return 0;

        if (source.data[0] == '-') result = -result;
        else if (source.data[0] != '+') return 0;
    }

    *out = result;

    return 1;
}

HELIOS_DEF B32 HeliosParseS64(HeliosStringView sv, U8 base, S64 *out) {
    if (base == 16) return _HeliosParseS64Hex(sv, out);
    return _HeliosParseS64Generic(sv, base, out);
}

HELIOS_DEF B32 HeliosParseS64DetectBase(HeliosStringView sv, S64 *out) {
    if (HeliosStringViewStartsWith(sv, "0x")) {
        sv.data += 2;
        sv.count -= 2;
        return _HeliosParseS64Hex(sv, out);
    }

    if (HeliosStringViewStartsWith(sv, "0o")) {
        sv.data += 2;
        sv.count -= 2;

        return _HeliosParseS64Generic(sv, 8, out);
    }

    if (HeliosStringViewStartsWith(sv, "0b")) {
        sv.data += 2;
        sv.count -= 2;

        return _HeliosParseS64Generic(sv, 2, out);
    }

    return _HeliosParseS64Generic(sv, 10, out);
}

// FIXME: This should NOT allocate anything, even on the temp allocator.
// Skill issue.
HELIOS_DEF B32 HeliosParseF64(HeliosStringView source, F64 *out) {
    HeliosAllocator temp_alloc = HeliosGetTempAllocator();

    UZ temp_count = source.count + 1;
    char *temp = (char *)HeliosAlloc(temp_alloc, temp_count);
    memcpy(temp, (const void *)source.data, source.count);
    temp[temp_count] = '\0';

    char *d_out;
    *out = strtod(temp, &d_out);
    return temp != d_out;
}

HELIOS_DEF void *HeliosRawAlloc(UZ size) {
#ifdef HELIOS_PLATFORM_WINDOWS
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else // Assume posix.
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif // HELIOS_PLATFORM_WINDOWS
}

HELIOS_INTERNAL void *MallocStub(void *user, UZ size) {
    HELIOS_UNUSED(user);
    return memset(malloc(size), 0, size);
}

HELIOS_INTERNAL void FreeStub(void *user, void *ptr, UZ size) {
    HELIOS_UNUSED(user);
    HELIOS_UNUSED(size);
    free(ptr);
}

HELIOS_INTERNAL void *ReallocStub(void *user, void *ptr, UZ old_size, UZ new_size) {
    HELIOS_UNUSED(user);
    HELIOS_UNUSED(old_size);
    return realloc(ptr, new_size);
}

HELIOS_INTERNAL void _HeliosNopFreeStub(void *user, void *ptr, UZ size) {
    HELIOS_UNUSED(user);
    HELIOS_UNUSED(ptr);
    HELIOS_UNUSED(size);
}

HELIOS_DEF HeliosAllocator HeliosNewMallocAllocator(void) {
    return (HeliosAllocator) {
        .vtable = (HeliosAllocatorVTable) {
            .alloc = MallocStub,
            .free = FreeStub,
            .realloc = ReallocStub,
        },
        .data = NULL,
    };
}

typedef struct HeliosDynamicCircleBufferAllocator {
    void *buffer;
    UZ capacity;
    UZ offset;
} HeliosDynamicCircleBufferAllocator;

HELIOS_INTERNAL void *_HeliosDynamicCircleBufferAllocatorAlloc(void *a_ptr, UZ size) {
    HeliosDynamicCircleBufferAllocator *allocator = (HeliosDynamicCircleBufferAllocator *)a_ptr;
    if (allocator->buffer == NULL) {
        HELIOS_ASSERT(allocator->capacity % 2 == 0);
        allocator->buffer = HeliosRawAlloc(allocator->capacity);
    }

    size = HeliosRoundUp(size, sizeof(UZ));
    if (allocator->offset + size >= allocator->capacity) allocator->offset = 0;

    void *ptr = (void *)((U8 *)allocator->buffer + allocator->offset);
    allocator->offset += size;
    return memset(ptr, 0, size);
}

HELIOS_DEF HeliosAllocator HeliosNewDynamicCircleBufferAllocator(HeliosDynamicCircleBufferAllocator *allocator, UZ capacity) {
    capacity = HeliosRoundUp(capacity, HELIOS_PAGE_ALIGNMENT);

    allocator->buffer = NULL;
    allocator->capacity = capacity;
    allocator->offset = 0;

    return (HeliosAllocator) {
        .data = (void *)allocator,
        .vtable = (HeliosAllocatorVTable) {
            .alloc = _HeliosDynamicCircleBufferAllocatorAlloc,
            .free = _HeliosNopFreeStub,
            .realloc = NULL,
        },
    };
}

HELIOS_INTERNAL HeliosDynamicCircleBufferAllocator _helios_temp_impl = {
    .buffer = NULL,
    .capacity = HELIOS_PAGE_SIZE * 20,
    .offset = 0,
};

#if defined(HELIOS_COMPILER_MSVC) || defined(HELIOS_COMPILER_GCC)
HELIOS_DEF HeliosAllocator HeliosGetTempAllocator(void) {
    return (HeliosAllocator) {
        .data = (void *)&_helios_temp_impl,
        .vtable = (HeliosAllocatorVTable) {
            .alloc = _HeliosDynamicCircleBufferAllocatorAlloc,
            .free = _HeliosNopFreeStub,
            .realloc = NULL,
        },
    };
}

#else
HELIOS_INTERNAL HeliosAllocator _helios_temp_allocator = {
    .data = (void *)&_helios_temp_impl,
    .vtable = (HeliosAllocatorVTable) {
        .alloc = _HeliosDynamicCircleBufferAllocatorAlloc,
        .free = _HeliosNopFreeStub,
        .realloc = NULL,
    },
};

HELIOS_DEF HeliosAllocator HeliosGetTempAllocator(void) {
    return _helios_temp_allocator;
}
#endif

HELIOS_DEF void HeliosString8StreamInit(HeliosString8Stream *stream, const U8 *data, UZ count) {
    stream->data = data;
    stream->byte_offset = (UZ)-1;
    stream->char_offset = (UZ)-1;
    stream->count = count;
    stream->last_char_size = -1;
}

#define HELIOS_UTF8_MASK2 ((HeliosChar)0xC0)
#define HELIOS_UTF8_MASK3 ((HeliosChar)0xE0)
#define HELIOS_UTF8_MASK4 ((HeliosChar)0xF0)

// TODO: add more validations
HELIOS_DEF B32 HeliosString8StreamNext(HeliosString8Stream *stream, HeliosChar *c) {
    ++stream->byte_offset;
    if (stream->byte_offset >= stream->count) return 0;

    ++stream->char_offset;

    const U8 *ptr = stream->data + stream->byte_offset;

    U32 bytes = ((U32)ptr[0] << 0) | ((U32)ptr[1] << 8) | ((U32)ptr[2] << 16) | ((U32)ptr[3] << 24);

    if ((bytes & HELIOS_UTF8_MASK2) == HELIOS_UTF8_MASK2) {
        if (c != NULL) *c = ((bytes & 0x1F) << 6) | ((bytes & 0x3F00) >> 8);
        stream->byte_offset += 1;
        stream->last_char_size = 2;
        return 1;
    }

    if ((bytes & HELIOS_UTF8_MASK3) == HELIOS_UTF8_MASK3) {
        if (c != NULL) *c = ((bytes & 0x0F) << 12) | ((bytes & 0x3F00) >> 2) | ((bytes & 0x3F0000) >> 16);
        stream->byte_offset += 2;
        stream->last_char_size = 3;
        return 1;
    }

    if ((bytes & HELIOS_UTF8_MASK4) == HELIOS_UTF8_MASK4) {
        if (c != NULL) *c = ((bytes & 0x07) << 18) | ((bytes & 0x3F00) << 4) | ((bytes & 0x3F0000) >> 10) | ((bytes & 0x3F000000) >> 24);
        stream->byte_offset += 3;
        stream->last_char_size = 4;
        return 1;
    }

    if (c != NULL) *c = bytes & 0xFF;
    stream->last_char_size = 1;
    return 1;
}

HELIOS_DEF void HeliosString8StreamRetreat(HeliosString8Stream *s) {
    HELIOS_VERIFY(s->last_char_size != -1);
    s->byte_offset -= s->last_char_size;
    s->char_offset -= 1;
    s->last_char_size = -1;
}

HELIOS_DEF HeliosStringView HeliosReadEntireFile(HeliosAllocator allocator, HeliosStringView path) {
    HeliosAllocator temp = HeliosGetTempAllocator();
    char *path_cstr = HeliosStringViewCloneToCStr(temp, path);

    ssize_t fd = open(path_cstr, O_RDONLY);
    if (fd == -1) {
        return (HeliosStringView) {.data = NULL, .count = 0};
    }

    struct stat file_stat;
    int ret = fstat(fd, &file_stat);
    if (ret == -1) {
        return (HeliosStringView) {.data = NULL, .count = 0};
    }

    size_t file_size = file_stat.st_size;
    U8 *file_buf = HeliosAlloc(allocator, file_size);

    size_t total_bytes_read = 0;
    while (total_bytes_read != file_size) {
        ssize_t n = read(fd, &file_buf[total_bytes_read], file_size - total_bytes_read);
        if (n == -1) {
            HeliosFree(allocator, file_buf, file_size);
            return (HeliosStringView) {.data = NULL, .count = 0};
        }
        total_bytes_read += (size_t)n;
    }

    return (HeliosStringView) {.data = file_buf, .count = file_size};
}

#endif // HELIOS_IMPLEMENTATION
#endif // HELIOS_H
