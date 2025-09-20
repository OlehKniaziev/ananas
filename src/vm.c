#include "vm.h"

ERMIS_IMPL_HASHMAP(HeliosStringView, AnanasVM_Value, AnanasVM_EnvMap, HeliosStringViewEqual, AnanasFnv1Hash)
ERMIS_IMPL_ARRAY(AnanasGC_Entity *, AnanasVM_EntityArray)

static void RunStateInit(AnanasVM_RunState *rs,
                         AnanasVM_RunState *parent,
                         U8 *bytecode,
                         UZ bytecode_count,
                         AnanasLIR_CompiledModule module) {
    rs->parent = parent;
    rs->module = module;
    rs->bytecode = bytecode;
    rs->bytecode_count = bytecode_count;
    rs->ip = 0;
}

#define IS_INT(x) ((x) & 1)
#define TO_INT(x) ((SZ)(x) >> 1)
#define FROM_INT(x) ((AnanasVM_Value)(((SZ)(x) << 1) | 1))
#define INT(x) ({ \
    AnanasVM_Value _x = (x); \
    HELIOS_VERIFY(IS_INT(_x)); \
    TO_INT(_x); \
})

#define IS_ENTITY(x) (!IS_INT((x)))
#define TO_ENTITY(x) ((AnanasGC_Entity *)(x))
#define FROM_ENTITY(x) ((AnanasVM_Value)(x))
#define ENTITY(x) ({ \
    AnanasVM_Value _x = (x); \
    HELIOS_VERIFY(IS_ENTITY(_x)); \
    TO_ENTITY(_x); \
})

typedef B32 (*NativeLambda)(AnanasVM *, UZ);

#define DEFINE_NATIVE_LAMBDA(name) static B32 name(AnanasVM *vm, UZ nargs)
#define DECLARE_NATIVE_LAMBDA DEFINE_NATIVE_LAMBDA

#define ENUM_NATIVE_LAMBDAS \
    X("print", AnanasPrint)

typedef struct {
    B32 is_native;
    union {
        AnanasLIR_CompiledLambda bytecode;
        NativeLambda native;
    } u;
} LambdaEntity;

typedef struct {
    UZ count;
    U8 data[];
} StringEntity;

enum {
    LAMBDA_DESCRIPTOR,
    STRING_DESCRIPTOR,
};

static void Release(AnanasVM *vm, AnanasVM_Value value) {
    if (IS_ENTITY(value)) {
        AnanasGC_Entity *e = TO_ENTITY(value);

        HELIOS_VERIFY(e->rc > 0);

        if (e->rc == 1) {
            AnanasVM_EntityArrayPush(&vm->unreachable_entities, e);
        }

        --e->rc;
    }
}

static AnanasVM_Value Pop(AnanasVM *vm) {
    HELIOS_VERIFY(vm->sp > 0);
    AnanasVM_Value value = vm->stack[--vm->sp];
    return value;
}

static void Push(AnanasVM *vm, AnanasVM_Value value) {
    HELIOS_VERIFY(vm->sp < ANANAS_VM_STACK_MAX);
    vm->stack[vm->sp++] = value;

    if (IS_ENTITY(value)) {
        AnanasGC_Entity *e = TO_ENTITY(value);
        ++e->rc;
    }
}

static B32 ValueToBool(AnanasVM_Value val) {
    HELIOS_VERIFY(IS_INT(val));
    return (B32)TO_INT(val);
}

#define DEFAULT_ENV_SIZE 53

static AnanasVM_Env *AllocEnv(AnanasVM *vm) {
    if (vm->env_pool == NULL) {
        AnanasVM_Env *env = HeliosAlloc(vm->allocator, sizeof(AnanasVM_Env));
        env->parent = NULL;
        AnanasVM_EnvMapInit(&env->map, vm->allocator, DEFAULT_ENV_SIZE);
        return env;
    }

    AnanasVM_Env *env = vm->env_pool;
    AnanasVM_EnvMapClear(&env->map);
    vm->env_pool = env->parent;
    env->parent = NULL;
    return env;
}

static void ReturnEnv(AnanasVM *vm, AnanasVM_Env *env) {
    env->parent = vm->env_pool;
    vm->env_pool = env;
}

DEFINE_NATIVE_LAMBDA(AnanasPrint) {
    HELIOS_VERIFY(nargs == 1);

    AnanasVM_Value val = Pop(vm);
    if (IS_INT(val)) {
        S64 i = TO_INT(val);
        printf("%ld\n", i);
    } else {
        AnanasGC_Entity *e = TO_ENTITY(val);
        HELIOS_ASSERT(e->descriptor == STRING_DESCRIPTOR);
        StringEntity *s = (StringEntity *)e->data;
        printf("\"" HELIOS_SV_FMT "\"\n", HELIOS_SV_ARG(*s));
    }

    Push(vm, val);
    return 1;
}

static AnanasVM_RunState *AllocRunState(AnanasVM *vm,
                                        AnanasVM_RunState *parent,
                                        U8 *bytecode,
                                        UZ bytecode_count,
                                        AnanasLIR_CompiledModule module) {
    if (vm->rs_pool == NULL) {
        AnanasVM_RunState *rs = HeliosAlloc(vm->allocator, sizeof(AnanasVM_RunState));
        RunStateInit(rs, parent, bytecode, bytecode_count, module);
        return rs;
    }

    AnanasVM_RunState *rs = vm->rs_pool;
    vm->rs_pool = rs->parent;
    RunStateInit(rs, parent, bytecode, bytecode_count, module);
    return rs;
}

static void ReturnRunState(AnanasVM *vm, AnanasVM_RunState *rs) {
    rs->parent = vm->rs_pool;
    vm->rs_pool = rs;
}

static B32 EnvInsert(AnanasVM_Env *env, HeliosStringView name, AnanasVM_Value value) {
    if (IS_ENTITY(value)) {
        AnanasGC_Entity *e = TO_ENTITY(value);
        ++e->rc;
    }

    return AnanasVM_EnvMapInsert(&env->map, name, value);
}

B32 EnvLookup(AnanasVM_Env *env, HeliosStringView name, AnanasVM_Value *value) {
    while (env != NULL) {
        if (AnanasVM_EnvMapFind(&env->map, name, value)) return 1;
        env = env->parent;
    }

    return 0;
}

static B32 Run(AnanasVM *vm, AnanasVM_RunState *rs) {
    while (rs->ip < rs->bytecode_count) {
        AnanasLIR_Op *op = (AnanasLIR_Op *)(rs->bytecode + rs->ip);
        switch (*op) {
        case AnanasLIR_Op_Add: {
            SZ rhs = INT(Pop(vm));
            SZ lhs = INT(Pop(vm));
            SZ result = lhs + rhs;
            Push(vm, FROM_INT(result));
            rs->ip += sizeof(*op);
            break;
        }
        case AnanasLIR_Op_Sub: {
            SZ rhs = INT(Pop(vm));
            SZ lhs = INT(Pop(vm));
            SZ result = lhs - rhs;
            Push(vm, FROM_INT(result));
            rs->ip += sizeof(*op);
            break;
        }
        case AnanasLIR_Op_Mul: {
            SZ rhs = INT(Pop(vm));
            SZ lhs = INT(Pop(vm));
            SZ result = lhs * rhs;
            Push(vm, FROM_INT(result));
            rs->ip += sizeof(*op);
            break;
        }
        case AnanasLIR_Op_Rem: {
            SZ rhs = INT(Pop(vm));
            SZ lhs = INT(Pop(vm));
            SZ result = lhs % rhs;
            Push(vm, FROM_INT(result));
            rs->ip += sizeof(*op);
            break;
        }
        case AnanasLIR_Op_Const: {
            AnanasLIR_OpConst *cop = (AnanasLIR_OpConst *)op;
            switch (cop->value.type) {
            case AnanasValueType_Int: {
                S64 i = cop->value.u.integer;
                Push(vm, FROM_INT(i));
                break;
            }
            case AnanasValueType_String: {
                HeliosStringView sv = cop->value.u.string;
                AnanasGC_Entity *e = AnanasGC_AllocEntity(vm->allocator,
                                                          sizeof(StringEntity) + sv.count + 1,
                                                          STRING_DESCRIPTOR);

                StringEntity *s = (StringEntity *)e->data;
                s->count = sv.count;
                memcpy(s->data, sv.data, sv.count);
                s->data[s->count] = '\0';

                Push(vm, FROM_ENTITY(e));
                break;
            }
            case AnanasValueType_Function:
            case AnanasValueType_Macro:
                HELIOS_UNREACHABLE();
            default: HELIOS_TODO();
            }

            rs->ip += sizeof(*cop);
            break;
        }
        case AnanasLIR_Op_Define: {
            AnanasLIR_OpDefine *dop = (AnanasLIR_OpDefine *)op;
            AnanasVM_Value value = Pop(vm);
            EnvInsert(vm->env, dop->name, value);
            Release(vm, value);
            rs->ip += sizeof(*dop);
            break;
        }
        case AnanasLIR_Op_Lookup: {
            AnanasLIR_OpLookup *lop = (AnanasLIR_OpLookup *)op;
            AnanasVM_Value value;
            HELIOS_VERIFY(EnvLookup(vm->env, lop->name, &value));
            Push(vm, value);
            rs->ip += sizeof(*lop);
            break;
        }
        case AnanasLIR_Op_Update: {
            AnanasLIR_OpUpdate *uop = (AnanasLIR_OpUpdate *)op;
            AnanasVM_Value value = Pop(vm);
            HELIOS_VERIFY(!EnvInsert(vm->env, uop->name, value));
            Release(vm, value);
            rs->ip += sizeof(*uop);
            break;
        }
        case AnanasLIR_Op_Jmp: {
            AnanasLIR_OpJmp *jop = (AnanasLIR_OpJmp *)op;
            rs->ip = jop->ip;
            break;
        }
        case AnanasLIR_Op_CondJmp: {
            AnanasLIR_OpCondJmp *jop = (AnanasLIR_OpCondJmp *)op;
            AnanasVM_Value cond_value = Pop(vm);
            B32 cond = ValueToBool(cond_value);
            if (cond) {
                rs->ip = jop->ip;
            } else {
                rs->ip += sizeof(*jop);
            }
            Release(vm, cond_value);
            break;
        }
        case AnanasLIR_Op_PushScope: {
            AnanasVM_Env *env = AllocEnv(vm);
            env->parent = vm->env;
            vm->env = env;
            rs->ip += sizeof(*op);
            break;
        }
        case AnanasLIR_Op_PopScope: {
            HELIOS_VERIFY(vm->env != NULL);
            AnanasVM_Env *env = vm->env;
            vm->env = vm->env->parent;
            ReturnEnv(vm, env);
            rs->ip += sizeof(*op);
            break;
        }
        case AnanasLIR_Op_LoadLambda: {
            AnanasLIR_OpLoadLambda *lop = (AnanasLIR_OpLoadLambda *)op;

            HELIOS_VERIFY(lop->index < rs->module.lambdas_count);

            LambdaEntity lam = {0};
            lam.is_native = 0;
            lam.u.bytecode = rs->module.lambdas[lop->index];

            AnanasGC_Entity *e = AnanasGC_AllocEntity(vm->allocator,
                                                      sizeof(lam),
                                                      LAMBDA_DESCRIPTOR);
            memcpy(e->data, &lam, sizeof(lam));
            Push(vm, FROM_ENTITY(e));

            rs->ip += sizeof(*lop);
            break;
        }
        case AnanasLIR_Op_Return: {
            // TODO(oleh): Push false if the callee did not push anything.
            HELIOS_VERIFY(rs->parent != NULL);
            AnanasVM_RunState *prev_rs = rs;
            rs = prev_rs->parent;
            ReturnRunState(vm, prev_rs);
            break;
        }
        case AnanasLIR_Op_Call: {
            AnanasLIR_OpCall *cop = (AnanasLIR_OpCall *)op;

            AnanasVM_Value lam_value = Pop(vm);
            AnanasGC_Entity *lam_e = ENTITY(lam_value);
            HELIOS_VERIFY(lam_e->descriptor == LAMBDA_DESCRIPTOR);

            Release(vm, lam_value);

            rs->ip += sizeof(*cop);

            LambdaEntity *lam = (LambdaEntity *)lam_e->data;
            if (!lam->is_native) {
                AnanasLIR_CompiledLambda blam = lam->u.bytecode;
                AnanasVM_RunState *new_rs = AllocRunState(vm, rs, blam.bytecode, blam.bytecode_count, rs->module);
                rs = new_rs;
            } else {
                NativeLambda native = lam->u.native;
                native(vm, cop->args_count);
            }

            break;
        }
        }

        for (UZ i = 0; i < vm->unreachable_entities.count; ++i) {
            AnanasGC_Entity *e = AnanasVM_EntityArrayAt(&vm->unreachable_entities, i);
            AnanasGC_FreeEntity(vm->allocator, e);
        }

        vm->unreachable_entities.count = 0;
    }

    return 1;
}

B32 AnanasVM_ExecModule(AnanasVM *vm, AnanasLIR_CompiledModule module) {
    AnanasVM_RunState rs = {0};
    RunStateInit(&rs, NULL, module.bytecode, module.bytecode_count, module);
    return Run(vm, &rs);
}

static void EnvInitRoot(AnanasVM_Env *env, HeliosAllocator allocator) {
    env->parent = NULL;
    AnanasVM_EnvMapInit(&env->map, allocator, DEFAULT_ENV_SIZE * 5);

    #define X(name, lam) do { \
        LambdaEntity lam_e = {0}; \
        lam_e.is_native = 1; \
        lam_e.u.native = lam; \
        AnanasGC_Entity *e = AnanasGC_AllocEntity(allocator, sizeof(lam_e), LAMBDA_DESCRIPTOR); \
        memcpy(e->data, &lam_e, sizeof(lam_e)); \
        AnanasVM_Value lam_val = FROM_ENTITY(e); \
        EnvInsert(env, HELIOS_SV_LIT(name), lam_val); \
    } while (0);
    ENUM_NATIVE_LAMBDAS
    #undef X
}

void AnanasVM_Init(AnanasVM *vm, HeliosAllocator allocator) {
    vm->allocator = allocator;

    vm->stack = HeliosAlloc(allocator, sizeof(*vm->stack) * ANANAS_VM_STACK_MAX);
    vm->sp = 0;

    vm->env_pool = NULL;
    vm->env = HeliosAlloc(allocator, sizeof(*vm->env));
    EnvInitRoot(vm->env, allocator);

    vm->rs_pool = NULL;
    AnanasVM_EntityArrayInit(&vm->unreachable_entities, allocator, 10);
}
