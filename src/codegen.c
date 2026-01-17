#include "codegen.h"

static B32 CG_CompileValue(AnanasCG_Context *ctx,
                           AnanasValue value,
                           CompiledValue *cval,
                           AnanasErrorContext *err_ctx) {
    switch (value.type) {
    case AnanasValueType_Int:

    }
}

typedef enum {
    ValueType_Imm,
    ValueType_Reg,
    ValueType_Label,
} ValueType;

typedef enum {
} Reg;

typedef struct {
    ValueType type;
    union {
        HeliosStringView imm;
        Reg reg;
        HeliosStringView label;
    } u
} Value;

static Value CompileConst(AnanasCG_Context *ctx, AnanasValue cv) {
    switch (cv.type) {
    }
}

B32 AnanasCG_CompileModule(AnanasCG_Context *ctx,
                           AnanasLIR_CompiledModule *mod,
                           AnanasCG_Program *program,
                           AnanasErrorContext *err_ctx) {
    UZ off = 0;
    while (off < mod->bytecode_count) {
        AnanasLIR_Op *op = &mod->bytecode[off];

        switch (*op) {
        case AnanasLIR_Op_Const:
            AnanasLIR_OpConst *cop = (AnanasLIR_OpConst *)op;

            Value v = CompileConst(ctx, cop->value);
            CompilePush(ctx, v);

            off += sizeof(*cop);
            break;
        }
    }
}
