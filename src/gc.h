#ifndef ANANAS_GC_H_
#define ANANAS_GC_H_

#include "astron.h"

typedef struct {
    HeliosAllocator backing;
} AnanasGC_Allocator;

typedef U64 AnanasGC_EntityDescriptor;

#ifdef HELIOS_BITS_32
#error "can't compile current gc on a 32-bit platform"
#endif // HELIOS_BITS_32

#define ANANAS_GC_CACHE_LINE_SIZE 64

typedef struct {
    _Atomic(UZ) rc;
    UZ size;
    AnanasGC_EntityDescriptor descriptor;
    U8 data[];
} AnanasGC_Entity;

AnanasGC_Entity *AnanasGC_AllocEntity(HeliosAllocator, UZ, AnanasGC_EntityDescriptor);
void AnanasGC_FreeEntity(HeliosAllocator, AnanasGC_Entity *);

HELIOS_INLINE HeliosAllocator AnanasGC_NewAllocator(AnanasGC_Allocator *allocator, HeliosAllocator backing) {
    HELIOS_TODO();

    allocator->backing = backing;

    return (HeliosAllocator) {
        .data = allocator,
        .vtable = {
        },
    };
}

#endif // ANANAS_GC_H_
