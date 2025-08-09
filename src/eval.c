#include "eval.h"

static U64 Fnv1Hash(HeliosStringView sv) {
    const U64 fnv_offset_basis = 0xCBF29CE484222325;
    const U64 fnv_prime = 0x100000001B3;

    U64 hash = fnv_offset_basis;

    for (UZ i = 0; i < sv.count; ++i) {
        hash *= fnv_prime;

        U8 byte = sv.data[i];
        hash ^= (U64)byte;
    }

    return hash;
}

ERMIS_IMPL_HASHMAP(HeliosStringView, AnanasASTNode, AnanasEnvMap, HeliosStringViewEqual, Fnv1Hash)

static B32 AnanasEnvLookup(AnanasEnv *env, HeliosStringView name, AnanasASTNode *node) {
    while (env != NULL) {
        if (AnanasEnvMapFind(&env->map, name, node)) return 1;
        env = env->parent_env;
    }

    return 0;
}

void AnanasEnvInit(AnanasEnv *env, AnanasEnv *parent_env, HeliosAllocator allocator) {
    AnanasEnvMapInit(&env->map, allocator, 37);
    env->parent_env = parent_env;
}

static B32 AnanasConvertToBool(AnanasASTNode node) {
    switch (node.type) {
    case AnanasASTNodeType_Int: return node.u.integer;

    case AnanasASTNodeType_Symbol:
    case AnanasASTNodeType_List:
    case AnanasASTNodeType_String:
    case AnanasASTNodeType_Function:
    case AnanasASTNodeType_Macro:
        return 1;
    }
}

static B32 AnanasEvalFormList(AnanasList *form_list,
                              AnanasArena *arena,
                              AnanasEnv *env,
                              AnanasErrorContext *error_ctx,
                              AnanasASTNode *result) {
    HELIOS_VERIFY(form_list != NULL);

    while (form_list != NULL) {
        if (!AnanasEval(form_list->car, arena, env, result, error_ctx)) return 0;
        form_list = form_list->cdr;
    }

    return 1;
}

static B32 AnanasEvalFunctionWithArgumentList(AnanasFunction *function,
                                              AnanasList *args_list,
                                              AnanasToken where,
                                              AnanasArena *arena,
                                              AnanasEnv *env,
                                              AnanasErrorContext *error_ctx,
                                              AnanasASTNode *result) {
    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

    AnanasEnv call_env;
    AnanasEnvInit(&call_env, function->enclosing_env, arena_allocator);

    UZ arguments_count = 0;

    while (args_list != NULL) {
        if (arguments_count >= function->params.count) {
            AnanasErrorContextMessage(error_ctx,
                                      where.row,
                                      where.col,
                                      "Too many arguments: expected %zu, got %zu",
                                      function->params.count,
                                      arguments_count);
            return 0;
        }

        AnanasASTNode param_value;
        if (!AnanasEval(args_list->car, arena, env, &param_value, error_ctx)) return 0;

        HeliosStringView param_name = function->params.names[arguments_count];
        AnanasEnvMapInsert(&call_env.map, param_name, param_value);

        arguments_count++;
        args_list = args_list->cdr;
    }

    if (arguments_count != function->params.count) {
        AnanasErrorContextMessage(error_ctx,
                                  where.row,
                                  where.col,
                                  "Not enough arguments: expected %zu, got %zu",
                                  function->params.count,
                                  arguments_count);
        return 0;
    }

    AnanasList *function_body = function->body;
    return AnanasEvalFormList(function_body,
                              arena,
                              &call_env,
                              error_ctx,
                              result);
}

static B32 AnanasEvalMacroWithArgumentList(AnanasMacro *macro,
                                           AnanasToken where,
                                           AnanasList *args_list,
                                           AnanasArena *arena,
                                           AnanasErrorContext *error_ctx,
                                           AnanasASTNode *result) {
    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

    AnanasEnv call_env;
    AnanasEnvInit(&call_env, macro->enclosing_env, arena_allocator);

    UZ args_count = 0;
    while (args_list != NULL) {
        if (args_count >= macro->params.count) {
            AnanasErrorContextMessage(error_ctx,
                                      where.row,
                                      where.col,
                                      "Too many arguments for macro call: expected '%zu', got %zu instead",
                                      args_count,
                                      macro->params.count);
            return 0;
        }

        HeliosStringView param_name = macro->params.names[args_count];
        AnanasEnvMapInsert(&call_env.map, param_name, args_list->car);

        ++args_count;
        args_list = args_list->cdr;
    }

    if (args_count != macro->params.count) {
        AnanasErrorContextMessage(error_ctx,
                                  where.row,
                                  where.col,
                                  "Not enough arguments for macro call: expected '%zu', got %zu instead",
                                  macro->params.count,
                                  args_count);
        return 0;
    }

    return AnanasEvalFormList(macro->body,
                              arena,
                              &call_env,
                              error_ctx,
                              result);
}

ERMIS_DECL_ARRAY(HeliosStringView, AnanasParamsArray)
ERMIS_IMPL_ARRAY(HeliosStringView, AnanasParamsArray)

static B32 AnanasParseParamsFromList(AnanasArena *arena,
                                     AnanasList *params_list,
                                     AnanasParams *out_params,
                                     AnanasErrorContext *error_ctx) {
    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

    AnanasParamsArray params_array;
    AnanasParamsArrayInit(&params_array, arena_allocator, 16);

    while (params_list != NULL) {
        AnanasASTNode param_node = params_list->car;
        if (param_node.type != AnanasASTNodeType_Symbol) {
            AnanasErrorContextMessage(error_ctx, param_node.token.row, param_node.token.col, "expected a symbol as a parameter name");
            return 0;
        }

        HeliosStringView param = param_node.u.string;
        AnanasParamsArrayPush(&params_array, param);

        params_list = params_list->cdr;
    }

    out_params->names = params_array.items;
    out_params->count = params_array.count;

    return 1;
}

B32 AnanasEval(AnanasASTNode node, AnanasArena *arena, AnanasEnv *env, AnanasASTNode *result, AnanasErrorContext *error_ctx) {
    switch (node.type) {
    case AnanasASTNodeType_Macro:
    case AnanasASTNodeType_Function:
    case AnanasASTNodeType_String:
    case AnanasASTNodeType_Int: {
        *result = node;
        return 1;
    }
    case AnanasASTNodeType_Symbol: {
        if (!AnanasEnvLookup(env, node.u.symbol, result)) {
            AnanasErrorContextMessage(error_ctx,
                                      node.token.row,
                                      node.token.col,
                                      "Unbound symbol '" HELIOS_SV_FMT "'",
                                      HELIOS_SV_ARG(node.u.symbol));
            return 0;
        }

        return 1;
    }
    case AnanasASTNodeType_List: {
        AnanasList *list = node.u.list;
        if (list == NULL) {
            AnanasErrorContextMessage(error_ctx,
                                      node.token.row,
                                      node.token.col,
                                      "Cannot evaluate a nil list");
            return 0;
        }

        if (list->car.type != AnanasASTNodeType_Symbol) {
            AnanasASTNode function_node;
            if (!AnanasEval(list->car, arena, env, &function_node, error_ctx)) return 0;

            if (function_node.type != AnanasASTNodeType_Function) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "List's car does not evaluate to a function");
                return 0;
            }

            AnanasFunction *function = function_node.u.function;

            return AnanasEvalFunctionWithArgumentList(function,
                                                      list->cdr,
                                                      node.token,
                                                      arena,
                                                      env,
                                                      error_ctx,
                                                      result);
        }

        HeliosStringView sym_name = list->car.u.symbol;
        if (HeliosStringViewEqualCStr(sym_name, "var")) {
            AnanasList *var_name_cons = list->cdr;
            if (var_name_cons == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'var' should have a variable name");
                return 0;
            }

            if (var_name_cons->car.type != AnanasASTNodeType_Symbol) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'var' name should be a symbol");
                return 0;
            }

            HeliosStringView var_name = var_name_cons->car.u.symbol;

            AnanasList *var_value_cons = var_name_cons->cdr;
            if (var_value_cons == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'var' should have a variable value");
                return 0;
            }

            AnanasASTNode var_value;
            if (!AnanasEval(var_value_cons->car, arena, env, &var_value, error_ctx)) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'var' value eval error");
                return 0;
            }

            AnanasEnvMapInsert(&env->map, var_name, var_value);

            *result = var_value;

            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "lambda")) {
            AnanasList *lambda_params_cons = list->cdr;
            if (lambda_params_cons == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'lambda' should have an arg list");
                return 0;
            }

            if (lambda_params_cons->car.type != AnanasASTNodeType_List) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'lambda' params should be a list");
                return 0;
            }

            AnanasList *lambda_params_list = lambda_params_cons->car.u.list;

            AnanasList *lambda_body = lambda_params_cons->cdr;
            if (lambda_body == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'lambda' should have a body");
                return 0;
            }

            AnanasParams lambda_params;
            if (!AnanasParseParamsFromList(arena, lambda_params_list, &lambda_params, error_ctx)) return 0;

            AnanasFunction *lambda = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasFunction);
            lambda->params = lambda_params;
            lambda->body = lambda_body;
            lambda->enclosing_env = env;

            result->type = AnanasASTNodeType_Function;
            result->u.function = lambda;

            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "or")) {
            AnanasList *args_list = list->cdr;

            AnanasASTNode truthy_node = {.type = AnanasASTNodeType_Int, .u = {.integer = 0}};

            while (args_list != NULL) {
                AnanasASTNode car;
                if (!AnanasEval(args_list->car, arena, env, &car, error_ctx)) return 0;

                if (AnanasConvertToBool(car)) {
                    truthy_node = car;
                    break;
                }

                args_list = args_list->cdr;
            }

            *result = truthy_node;
            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "and")) {
            AnanasList *args_list = list->cdr;

            AnanasASTNode falsy_node = {.type = AnanasASTNodeType_Int, .u = {.integer = 0}};

            while (args_list != NULL) {
                AnanasASTNode car;
                if (!AnanasEval(args_list->car, arena, env, &car, error_ctx)) return 0;

                falsy_node = car;

                if (!AnanasConvertToBool(car)) {
                    break;
                }

                args_list = args_list->cdr;
            }

            *result = falsy_node;
            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "macro")) {
            AnanasList *args_list = list->cdr;
            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no name passed to 'macro' form");
                return 0;
            }

            AnanasASTNode macro_name_node = args_list->car;
            if (macro_name_node.type != AnanasASTNodeType_Symbol) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'macro' name should be a symbol");
                return 0;
            }

            HeliosStringView macro_name = macro_name_node.u.string;

            args_list = args_list->cdr;
            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no arguments passed to 'macro' form");
                return 0;
            }

            AnanasASTNode macro_args_node = args_list->car;
            if (macro_args_node.type != AnanasASTNodeType_List) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'macro' form arguments should be a list");
                return 0;
            }

            AnanasList *macro_params_list = macro_args_node.u.list;
            AnanasParams macro_params;
            if (!AnanasParseParamsFromList(arena, macro_params_list, &macro_params, error_ctx)) return 0;

            AnanasList *macro_body = args_list->cdr;

            AnanasMacro *macro = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasMacro);
            macro->body = macro_body;
            macro->enclosing_env = env;
            macro->params = macro_params;

            AnanasASTNode macro_node = {
                .type = AnanasASTNodeType_Macro,
                .u = {
                    .macro = macro,
                },
            };

            AnanasEnvMapInsert(&env->map, macro_name, macro_node);
            *result = macro_node;
            return 1;
        } else {
            AnanasASTNode callable_node;
            if (!AnanasEnvLookup(env, sym_name, &callable_node)) {
                AnanasToken token = list->car.token;
                AnanasErrorContextMessage(error_ctx,
                                          token.row,
                                          token.col,
                                          "Unbound symbol '" HELIOS_SV_FMT "'",
                                          HELIOS_SV_ARG(sym_name));
                return 0;
            }

            if (callable_node.type == AnanasASTNodeType_Function) {
                AnanasFunction *function = callable_node.u.function;
                return AnanasEvalFunctionWithArgumentList(function,
                                                          list->cdr,
                                                          node.token,
                                                          arena,
                                                          env,
                                                          error_ctx,
                                                          result);
            } else if (callable_node.type == AnanasASTNodeType_Macro) {
                AnanasMacro *macro = callable_node.u.macro;
                return AnanasEvalMacroWithArgumentList(macro,
                                                       node.token,
                                                       list->cdr,
                                                       arena,
                                                       error_ctx,
                                                       result);
            } else {
                AnanasToken token = list->car.token;
                AnanasErrorContextMessage(error_ctx,
                                          token.row,
                                          token.col,
                                          "value of symbol '" HELIOS_SV_FMT "' is not callable",
                                          HELIOS_SV_ARG(sym_name));
                return 0;
            }
        }
    }
    }
}

HeliosStringView AnanasPrint(HeliosAllocator allocator, AnanasASTNode node) {
    switch (node.type) {
    case AnanasASTNodeType_Int: {
        int required_bytes = snprintf(NULL, 0, "%ld", node.u.integer);
        U8 *buffer = HeliosAlloc(allocator, required_bytes + 1);
        sprintf((char *)buffer, "%ld", node.u.integer);
        return (HeliosStringView) {.data = buffer, .count = required_bytes};
    }
    case AnanasASTNodeType_String: {
        int required_bytes = snprintf(NULL, 0, "\"" HELIOS_SV_FMT "\"", HELIOS_SV_ARG(node.u.string));
        U8 *buffer = HeliosAlloc(allocator, required_bytes + 1);
        sprintf((char *)buffer, "\"" HELIOS_SV_FMT "\"", HELIOS_SV_ARG(node.u.string));
        return (HeliosStringView) {.data = buffer, .count = required_bytes};

    }
    case AnanasASTNodeType_Symbol: {
        int required_bytes = snprintf(NULL, 0, HELIOS_SV_FMT, HELIOS_SV_ARG(node.u.symbol));
        U8 *buffer = HeliosAlloc(allocator, required_bytes + 1);
        sprintf((char *)buffer, HELIOS_SV_FMT, HELIOS_SV_ARG(node.u.symbol));
        return (HeliosStringView) {.data = buffer, .count = required_bytes};
    }
    case AnanasASTNodeType_Macro: {
        return HELIOS_SV_LIT("<macro>");
    }
    case AnanasASTNodeType_Function: {
        /* int required_bytes = snprintf(NULL, 0, "", HELIOS_SV_ARG(node.u.symbol)); */
        /* U8 *buffer = HeliosAlloc(allocator, required_bytes + 1); */
        /* sprintf((char *)buffer, HELIOS_SV_FMT, HELIOS_SV_ARG(node.u.symbol)); */
        /* return (HeliosStringView) {.data = buffer, .count = required_bytes}; */
        return HELIOS_SV_LIT("<function>");
    }
    case AnanasASTNodeType_List: {
        UZ buffer_cap = 32;
        U8 *buffer = HeliosAlloc(allocator, buffer_cap);

        buffer[0] = '(';

        UZ offset = 1;

        AnanasList *list = node.u.list;
        while (list != NULL) {
            HeliosStringView car = AnanasPrint(allocator, list->car);
            if (car.count + offset + 1 >= buffer_cap) {
                buffer = HeliosRealloc(allocator, buffer, buffer_cap, buffer_cap * 2);
                buffer_cap *= 2;
            }

            for (UZ i = 0; i < car.count; ++i) {
                buffer[i + offset] = car.data[i];
            }

            if (list->cdr != NULL) {
                buffer[car.count + offset] = ' ';
                offset += car.count + 1;
            } else {
                offset += car.count;
            }

            list = list->cdr;
        }

        HELIOS_VERIFY(offset < buffer_cap);

        buffer[offset] = ')';

        return (HeliosStringView) {.data = buffer, .count = offset + 1};
    }
    }
}
