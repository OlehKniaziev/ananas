#include "gc.h"

AnanasGC_Entity *AnanasGC_AllocEntity(HeliosAllocator allocator,
                                      UZ size,
                                      AnanasGC_EntityDescriptor descriptor) {
    UZ alloc_size = sizeof(AnanasGC_Entity) + size;
    // NOTE(oleh): This should prevent false sharing when changing the rc.
    if (alloc_size < ANANAS_GC_CACHE_LINE_SIZE) alloc_size = ANANAS_GC_CACHE_LINE_SIZE;

    AnanasGC_Entity *e = HeliosAlloc(allocator, alloc_size);
    e->rc = 0;
    e->size = alloc_size - sizeof(AnanasGC_Entity);
    e->descriptor = descriptor;
    return e;
}

void AnanasGC_FreeEntity(HeliosAllocator allocator, AnanasGC_Entity *e) {
    HeliosFree(allocator, e, e->size + sizeof(AnanasGC_Entity));
}
