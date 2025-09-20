#ifndef ANANAS_VM_H_
#define ANANAS_VM_H_

#include "lir.h"
#include "gc.h"

#define ANANAS_VM_STACK_MAX (1024 * 1024 / sizeof(AnanasVM_Value))

typedef UZ AnanasVM_Value;

_Static_assert(sizeof(AnanasVM_Value) == sizeof(void *), "size of value should be equal to size of machine word");

ERMIS_DECL_HASHMAP(HeliosStringView, AnanasVM_Value, AnanasVM_EnvMap)

typedef struct AnanasVM_Env {
    struct AnanasVM_Env *parent;
    AnanasVM_EnvMap map;
} AnanasVM_Env;

ERMIS_DECL_ARRAY(AnanasGC_Entity *, AnanasVM_EntityArray)

typedef struct AnanasVM_RunState {
    struct AnanasVM_RunState *parent;
    UZ ip;

    U8 *bytecode;
    UZ bytecode_count;

    AnanasLIR_CompiledModule module;
} AnanasVM_RunState;

typedef struct {
    HeliosAllocator allocator;

    AnanasVM_Value *stack;
    UZ sp;

    AnanasVM_Env *env;
    AnanasVM_Env *env_pool;

    AnanasVM_RunState *rs_pool;

    AnanasVM_EntityArray unreachable_entities;
} AnanasVM;

void AnanasVM_Init(AnanasVM *, HeliosAllocator);

B32 AnanasVM_ExecModule(AnanasVM *vm, AnanasLIR_CompiledModule module);

#endif // ANANAS_VM_H_
