#include "interpreter.h"

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

B32 AnanasEval(AnanasASTNode node, AnanasArena *arena, AnanasEnv *env, AnanasASTNode *result) {
    switch (node.type) {
    case AnanasASTNodeType_Function:
    case AnanasASTNodeType_String:
    case AnanasASTNodeType_Int: {
        *result = node;
        return 1;
    }
    case AnanasASTNodeType_Symbol: {
        if (!AnanasEnvLookup(env, node.u.symbol, result)) {
            printf("Failed to lookup " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(node.u.symbol));
            return 0;
        }

        return 1;
    }
    case AnanasASTNodeType_List: {
        AnanasList *list = node.u.list;
        if (list == NULL) {
            printf("Empty list\n");
            return 0;
        }

        if (list->car.type != AnanasASTNodeType_Symbol) {
            AnanasASTNode function_node;
            if (!AnanasEval(list->car, arena, env, &function_node)) return 0;

            if (function_node.type != AnanasASTNodeType_Function) {
                printf("List's car does not evaluate to a function\n");
                return 0;
            }

            AnanasFunction *function = function_node.u.function;

            HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

            AnanasEnv call_env;
            AnanasEnvInit(&call_env, env, arena_allocator);

            UZ arguments_count = 0;

            AnanasList *args_list = list->cdr;

            while (args_list != NULL) {
                if (arguments_count >= function->params_count) {
                    printf("Too many arguments: expected %zu, got %zu\n", function->params_count, arguments_count);
                    return 0;
                }

                AnanasASTNode param_value;
                if (!AnanasEval(args_list->car, arena, env, &param_value)) return 0;

                HeliosStringView param_name = function->params_names[arguments_count];
                AnanasEnvMapInsert(&call_env.map, param_name, param_value);

                arguments_count++;
                args_list = args_list->cdr;
            }

            if (arguments_count != function->params_count) {
                printf("Not enough arguments: expected %zu, got %zu\n", function->params_count, arguments_count);
                return 0;
            }

            return AnanasEval(function->body, arena, &call_env, result);
        }

        HeliosStringView sym_name = list->car.u.symbol;
        if (HeliosStringViewEqualCStr(sym_name, "var")) {
            AnanasList *var_name_cons = list->cdr;
            if (var_name_cons == NULL) {
                printf("'var' should have a variable name\n");
                return 0;
            }

            if (var_name_cons->car.type != AnanasASTNodeType_Symbol) {
                printf("'var' name should be a symbol\n");
                return 0;
            }

            HeliosStringView var_name = var_name_cons->car.u.symbol;

            AnanasList *var_value_cons = var_name_cons->cdr;
            if (var_value_cons == NULL) {
                printf("'var' should have a variable value\n");
                return 0;
            }

            AnanasASTNode var_value;
            if (!AnanasEval(var_value_cons->car, arena, env, &var_value)) {
                printf("'var' value eval error\n");
                return 0;
            }

            AnanasEnvMapInsert(&env->map, var_name, var_value);

            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "lambda")) {
            AnanasList *lambda_params_cons = list->cdr;
            if (lambda_params_cons == NULL) {
                printf("'lambda' should have an arg list\n");
                return 0;
            }

            if (lambda_params_cons->car.type != AnanasASTNodeType_List) {
                printf("'lambda' params should be a list\n");
                return 0;
            }

            AnanasList *lambda_params_list = lambda_params_cons->car.u.list;

            AnanasList *lambda_body_cons = lambda_params_cons->cdr;
            if (lambda_body_cons == NULL) {
                printf("'lambda' should have a body\n");
                return 0;
            }

            if (lambda_body_cons->cdr != NULL) {
                printf("'lambda' body should have only one form\n");
                return 0;
            }

            AnanasASTNode lambda_body = lambda_body_cons->car;

            UZ lambda_params_count = 0;
            HeliosStringView *lambda_params = AnanasArenaPush(arena, sizeof(HeliosStringView));
            while (lambda_params_list != NULL) {
                if (lambda_params_list->car.type != AnanasASTNodeType_Symbol) {
                    printf("'lambda' argument %zu is not a symbol\n", lambda_params_count);
                    return 0;
                }

                lambda_params[lambda_params_count] = lambda_params_list->car.u.symbol;
                lambda_params_count++;
                AnanasArenaPush(arena, sizeof(HeliosStringView));

                lambda_params_list = lambda_params_list->cdr;
            }

            AnanasFunction *lambda = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasFunction);
            lambda->params_names = lambda_params;
            lambda->params_count = lambda_params_count;
            lambda->body = lambda_body;
            lambda->enclosing_env = env;

            result->type = AnanasASTNodeType_Function;
            result->u.function = lambda;

            return 1;
        } else {
            HELIOS_PANIC_FMT("Unknown sform or function '" HELIOS_SV_FMT "'\n", HELIOS_SV_ARG(sym_name));
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
    case AnanasASTNodeType_Function: {
        /* int required_bytes = snprintf(NULL, 0, "", HELIOS_SV_ARG(node.u.symbol)); */
        /* U8 *buffer = HeliosAlloc(allocator, required_bytes + 1); */
        /* sprintf((char *)buffer, HELIOS_SV_FMT, HELIOS_SV_ARG(node.u.symbol)); */
        /* return (HeliosStringView) {.data = buffer, .count = required_bytes}; */
        HELIOS_TODO();
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
