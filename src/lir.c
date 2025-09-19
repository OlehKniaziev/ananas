#include "lir.h"

static void AppendOp(AnanasLIR_CompilerContext *ctx, void *op, UZ op_size) {
    UZ avail = ctx->bytecode.capacity - ctx->bytecode.count;
    if (avail < op_size) {
        UZ new_cap = HELIOS_MAX(ERMIS_ARRAY_GROW_FACTOR(ctx->bytecode.capacity), ctx->bytecode.capacity + op_size);
        ctx->bytecode.bytes = HeliosRealloc(ctx->arena, ctx->bytecode.bytes, ctx->bytecode.count, new_cap);
        ctx->bytecode.capacity = new_cap;
    }

    memcpy(ctx->bytecode.bytes + ctx->bytecode.count, op, op_size);
    ctx->bytecode.count += op_size;
}

#define APPEND_OP(op) do { \
        AppendOp(ctx, (void *)&(op), sizeof((op))); \
    } while (0)

#define APPEND_SIMPLE(op) do { \
        AnanasLIR_Op x = AnanasLIR_Op_##op; \
        APPEND_OP(x); \
    } while (0)

static void AddLambda(AnanasLIR_CompilerContext *ctx, AnanasLIR_CompiledLambda lam) {
    if (ctx->lambdas_count >= ctx->lambdas_capacity) {
        UZ new_cap = ERMIS_ARRAY_GROW_FACTOR(ctx->lambdas_capacity);
        ctx->lambdas = HeliosRealloc(ctx->arena,
                                     ctx->lambdas,
                                     sizeof(lam) * ctx->lambdas_count,
                                     sizeof(lam) * new_cap);
        ctx->lambdas_capacity = new_cap;
    }

    ctx->lambdas[ctx->lambdas_count++] = lam;
}

static B32 CompileValue(AnanasLIR_CompilerContext *ctx, AnanasValue value) {
    switch (value.type) {
    case AnanasValueType_Function:
    case AnanasValueType_Macro:
        HELIOS_TODO();
    case AnanasValueType_Bool:
    case AnanasValueType_String:
    case AnanasValueType_Int: {
        AnanasLIR_OpConst cop = {0};
        cop.op = AnanasLIR_Op_Const;
        cop.value = value;
        APPEND_OP(cop);
        return 1;
    }
    case AnanasValueType_Symbol: {
        AnanasLIR_OpLookup lop = {0};
        lop.op = AnanasLIR_Op_Lookup;
        lop.name = value.u.symbol;
        APPEND_OP(lop);
        return 1;
    }
    case AnanasValueType_List: {
#define COM_BINOP(op) do { \
    HELIOS_ASSERT(args != NULL); \
    \
    AnanasValue lhs = args->car; \
    if (!CompileValue(ctx, lhs)) return 0; \
    \
    HELIOS_ASSERT(args->cdr != NULL); \
    AnanasValue rhs = args->cdr->car; \
    if (!CompileValue(ctx, rhs)) return 0; \
    \
    APPEND_SIMPLE(op); \
    return 1; \
    } while (0)

        AnanasList *list = value.u.list;
        HELIOS_ASSERT(list != NULL);

        AnanasValue car_cons = list->car;
        HELIOS_ASSERT(car_cons.type == AnanasValueType_Symbol);

        HeliosStringView sym_name = car_cons.u.symbol;

        AnanasList *args = list->cdr;

        if (HeliosStringViewEqualCStr(sym_name, "var")) {
            HELIOS_ASSERT(args != NULL);

            AnanasValue var_name_cons = args->car;
            HELIOS_ASSERT(var_name_cons.type == AnanasValueType_Symbol);

            HeliosStringView var_name = var_name_cons.u.symbol;

            HELIOS_ASSERT(args->cdr != NULL);

            AnanasValue var_value = args->cdr->car;
            if (!CompileValue(ctx, var_value)) return 0;

            AnanasLIR_OpDefine dop = {0};
            dop.op = AnanasLIR_Op_Define;
            dop.name = var_name;
            APPEND_OP(dop);
            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "set")) {
            HELIOS_ASSERT(args != NULL);

            AnanasValue var_name_cons = args->car;
            HELIOS_ASSERT(var_name_cons.type == AnanasValueType_Symbol);

            HeliosStringView var_name = var_name_cons.u.symbol;

            HELIOS_ASSERT(args->cdr != NULL);

            AnanasValue var_value = args->cdr->car;
            if (!CompileValue(ctx, var_value)) return 0;

            AnanasLIR_OpInsert iop = {0};
            iop.op = AnanasLIR_Op_Insert;
            iop.name = var_name;
            APPEND_OP(iop);

            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "do")) {
            while (args != NULL) {
                if (!CompileValue(ctx, args->car)) return 0;
                args = args->cdr;
            }

            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "+")) {
            COM_BINOP(Add);
        } else if (HeliosStringViewEqualCStr(sym_name, "-")) {
            COM_BINOP(Sub);
        } else if (HeliosStringViewEqualCStr(sym_name, "*")) {
            COM_BINOP(Mul);
        } else if (HeliosStringViewEqualCStr(sym_name, "rem")) {
            COM_BINOP(Rem);
        } else if (HeliosStringViewEqualCStr(sym_name, "let")) {
            HELIOS_ASSERT(args != NULL);

            AnanasValue bindings_val = args->car;
            HELIOS_ASSERT(bindings_val.type == AnanasValueType_List);

            APPEND_SIMPLE(PushScope);

            AnanasList *bindings = bindings_val.u.list;
            while (bindings != NULL) {
                AnanasValue pair_val = bindings->car;
                HELIOS_ASSERT(pair_val.type == AnanasValueType_List);

                AnanasList *pair = pair_val.u.list;
                HELIOS_ASSERT(pair != NULL);
                HELIOS_ASSERT(pair->cdr != NULL);

                AnanasValue binding_name_val = pair->car;
                HELIOS_ASSERT(binding_name_val.type == AnanasValueType_Symbol);

                HeliosStringView binding_name = binding_name_val.u.symbol;

                AnanasValue binding_value = pair->cdr->car;
                if (!CompileValue(ctx, binding_value)) return 0;

                AnanasLIR_OpDefine dop = {0};
                dop.op = AnanasLIR_Op_Define;
                dop.name = binding_name;
                APPEND_OP(dop);

                bindings = bindings->cdr;
            }

            AnanasList *forms = args->cdr;

            while (forms != NULL) {
                if (!CompileValue(ctx, forms->car)) return 0;
                forms = forms->cdr;
            }

            APPEND_SIMPLE(PopScope);
            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "if")) {
            HELIOS_ASSERT(args != NULL);
            HELIOS_ASSERT(args->cdr != NULL);
            HELIOS_ASSERT(args->cdr->cdr != NULL);

            AnanasValue cond = args->car;
            if (!CompileValue(ctx, cond)) return 0;

            UZ cond_jump_consq_offset = ctx->bytecode.count;
            AnanasLIR_OpCondJmp cond_jump_consq = {0};
            cond_jump_consq.op = AnanasLIR_Op_CondJmp;
            APPEND_OP(cond_jump_consq);

            AnanasValue alt_val = args->cdr->cdr->car;
            if (!CompileValue(ctx, alt_val)) return 0;

            UZ jump_end_offset = ctx->bytecode.count;
            AnanasLIR_OpJmp jump_end = {0};
            jump_end.op = AnanasLIR_Op_Jmp;
            APPEND_OP(jump_end);

            AnanasLIR_OpCondJmp *cond_jump_consq_ptr = (AnanasLIR_OpCondJmp *)(ctx->bytecode.bytes + cond_jump_consq_offset);
            cond_jump_consq_ptr->ip = ctx->bytecode.count;

            AnanasValue consq_val = args->cdr->car;
            if (!CompileValue(ctx, consq_val)) return 0;

            AnanasLIR_OpJmp *jump_end_ptr = (AnanasLIR_OpJmp *)(ctx->bytecode.bytes + jump_end_offset);
            jump_end_ptr->ip = ctx->bytecode.count;

            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "lambda")) {
            HELIOS_ASSERT(args != NULL);

            AnanasLIR_Bytecode cur_bytecode = ctx->bytecode;
            memset(&ctx->bytecode, 0, sizeof(ctx->bytecode));

            AnanasValue params_val = args->car;
            HELIOS_ASSERT(params_val.type == AnanasValueType_List);
            AnanasList *params_list = params_val.u.list;

            AnanasLIR_CompiledLambda lambda = {0};
            HELIOS_ASSERT(AnanasParseParamsFromList(ctx->arena, params_list, &lambda.params, NULL));

            AnanasList *body = args->cdr;
            while (body != NULL) {
                if (!CompileValue(ctx, body->car)) return 0;
                body = body->cdr;
            }

            APPEND_SIMPLE(Return);

            lambda.bytecode = ctx->bytecode;
            ctx->bytecode = cur_bytecode;

            AnanasLIR_OpLoadLambda lop = {0};
            lop.op = AnanasLIR_Op_LoadLambda;
            lop.index = ctx->lambdas_count;
            APPEND_OP(lop);

            AddLambda(ctx, lambda);

            return 1;
        } else {
            while (args != NULL) {
                if (!CompileValue(ctx, args->car)) return 0;
                args = args->cdr;
            }

            AnanasLIR_OpLookup lop = {0};
            lop.op = AnanasLIR_Op_Lookup;
            lop.name = sym_name;
            APPEND_OP(lop);

            APPEND_SIMPLE(Call);
            return 1;
        }
    }
    }

    #undef COM_BINOP

    HELIOS_UNREACHABLE();
}

B32 AnanasLIR_CompileProgram(AnanasLIR_CompilerContext *ctx, AnanasValueArray prog) {
    for (UZ i = 0; i < prog.count; ++i) {
        AnanasValue value = AnanasValueArrayAt(&prog, i);
        if (!CompileValue(ctx, value)) return 0;
    }

    return 1;
}
