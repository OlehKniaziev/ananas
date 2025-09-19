#ifndef ANANAS_LIR_H_
#define ANANAS_LIR_H_

#include "astron.h"
#include "value.h"

#define ANANAS_LIR_ENUM_OPS \
    X(Const) \
    X(Add) \
    X(Sub) \
    X(Mul) \
    X(Rem) \
    X(Define) \
    X(Lookup) \
    X(Insert) \
    X(Call) \
    X(Return) \
    X(PushScope) \
    X(PopScope) \
    X(CondJmp) \
    X(Jmp) \
    X(LoadLambda)

typedef enum {
    #define X(op) AnanasLIR_Op_##op,
    ANANAS_LIR_ENUM_OPS
    #undef X
} AnanasLIR_Op;

typedef struct {
    AnanasLIR_Op op;
    AnanasValue value;
} AnanasLIR_OpConst;

typedef struct {
    AnanasLIR_Op op;
    HeliosStringView name;
} AnanasLIR_OpDefine;

typedef struct {
    AnanasLIR_Op op;
    HeliosStringView name;
} AnanasLIR_OpLookup;

typedef struct {
    AnanasLIR_Op op;
    HeliosStringView name;
} AnanasLIR_OpInsert;

typedef struct {
    AnanasLIR_Op op;
    UZ ip;
} AnanasLIR_OpJmp;

typedef struct {
    AnanasLIR_Op op;
    UZ ip;
} AnanasLIR_OpCondJmp;

typedef struct {
    AnanasLIR_Op op;
    U32 index;
} AnanasLIR_OpLoadLambda;

typedef struct {
    U8 *bytes;
    UZ count;
    UZ capacity;
} AnanasLIR_Bytecode;

typedef struct {
    AnanasLIR_Bytecode bytecode;
    AnanasParams params;
} AnanasLIR_CompiledLambda;

typedef struct {
    HeliosAllocator arena;
    AnanasArena *temp;

    AnanasLIR_Bytecode bytecode;

    AnanasLIR_CompiledLambda *lambdas;
    UZ lambdas_count;
    UZ lambdas_capacity;
} AnanasLIR_CompilerContext;

static inline const char *AnanasLIR_OpName(AnanasLIR_Op op) {
    switch (op) {
        #define X(op) case AnanasLIR_Op_##op: return #op;
        ANANAS_LIR_ENUM_OPS
        #undef X
    }

    HELIOS_UNREACHABLE();
}

static inline void AnanasLIR_CompilerContextInit(AnanasLIR_CompilerContext *ctx,
                                                 HeliosAllocator arena,
                                                 AnanasArena *temp) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->arena = arena;
    ctx->temp = temp;
}

B32 AnanasLIR_CompileProgram(AnanasLIR_CompilerContext *ctx, AnanasValueArray prog);

#endif // ANANAS_LIR_H_
