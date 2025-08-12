#include "eval.h"
#include "print.h"

ERMIS_IMPL_HASHMAP(HeliosStringView, AnanasValue, AnanasEnvMap, HeliosStringViewEqual, AnanasFnv1Hash)

static B32 AnanasEnvLookup(AnanasEnv *env, HeliosStringView name, AnanasValue *node) {
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

#define ANANAS_ENUM_NATIVE_FUNCTIONS \
    X("cons", AnanasCons) \
    X("read-file", AnanasReadFile) \
    X("list", AnanasListProc) \
    X("car", AnanasCar) \
    X("cdr", AnanasCdr) \
    X("string-split", AnanasStringSplit) \
    X("concat", AnanasConcat) \
    X("substring", AnanasSubstring) \
    X("print", AnanasPrintBuiltin) \
    X("read", AnanasRead) \
    X("=", AnanasEqualBuiltin)

#define X(name, func) ANANAS_DECLARE_NATIVE_FUNCTION(func);
ANANAS_ENUM_NATIVE_FUNCTIONS
#undef X

void AnanasRootEnvPopulate(AnanasEnv *env) {
    HeliosAllocator allocator = env->map.allocator;

    AnanasEnvMapInsert(&env->map, HELIOS_SV_LIT("true"), ANANAS_TRUE);
    AnanasEnvMapInsert(&env->map, HELIOS_SV_LIT("false"), ANANAS_FALSE);

#define X(name, func) { \
    AnanasFunction *native_func = HeliosAlloc(allocator, sizeof(AnanasFunction)); \
    native_func->is_native = 1; \
    native_func->u.native = func; \
    AnanasValue func_value = {.type = AnanasValueType_Function, .u = {.function = native_func}}; \
    AnanasEnvMapInsert(&env->map, HELIOS_SV_LIT(name), func_value); \
    }
ANANAS_ENUM_NATIVE_FUNCTIONS
#undef X
}

static B32 AnanasConvertToBool(AnanasValue node) {
    switch (node.type) {
    case AnanasValueType_Int: return node.u.integer;

    case AnanasValueType_Symbol:
    case AnanasValueType_List:
    case AnanasValueType_String:
    case AnanasValueType_Function:
    case AnanasValueType_Macro:
        return 1;

    case AnanasValueType_Bool: return node.u.boolean;
    }
}

static B32 AnanasEvalFormList(AnanasList *form_list,
                              AnanasArena *arena,
                              AnanasEnv *env,
                              AnanasErrorContext *error_ctx,
                              AnanasValue *result) {
    HELIOS_VERIFY(form_list != NULL);

    while (form_list != NULL) {
        if (!AnanasEval(form_list->car, arena, env, result, error_ctx)) return 0;
        form_list = form_list->cdr;
    }

    return 1;
}

ERMIS_DECL_ARRAY(AnanasValue, AnanasArgsArray)
ERMIS_IMPL_ARRAY(AnanasValue, AnanasArgsArray)

static B32 AnanasEvalFunctionWithArgumentList(AnanasFunction *function,
                                              AnanasList *args_list,
                                              AnanasToken where,
                                              AnanasArena *arena,
                                              AnanasEnv *env,
                                              AnanasErrorContext *error_ctx,
                                              AnanasValue *result) {
    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

    if (function->is_native) {
        AnanasArgsArray args_array;
        AnanasArgsArrayInit(&args_array, arena_allocator, 10);

        while (args_list != NULL) {
            AnanasValue arg_value;
            if (!AnanasEval(args_list->car, arena, env, &arg_value, error_ctx)) return 0;

            AnanasArgsArrayPush(&args_array, arg_value);

            args_list = args_list->cdr;
        }

        AnanasArgs call_args = {
            .values = args_array.items,
            .count = args_array.count,
        };

        AnanasNativeFunction native_function = function->u.native;
        return native_function(call_args, where, arena, error_ctx, result);
    }

    AnanasUserFunction user_function = function->u.user;

    AnanasEnv call_env;
    AnanasEnvInit(&call_env, user_function.enclosing_env, arena_allocator);

    UZ arguments_count = 0;

    while (args_list != NULL) {
        if (arguments_count >= user_function.params.count) {
            AnanasErrorContextMessage(error_ctx,
                                      where.row,
                                      where.col,
                                      "Too many arguments: expected %zu, got %zu",
                                      user_function.params.count,
                                      arguments_count);
            return 0;
        }

        AnanasValue param_value;
        if (!AnanasEval(args_list->car, arena, env, &param_value, error_ctx)) return 0;

        HeliosStringView param_name = user_function.params.names[arguments_count];
        AnanasEnvMapInsert(&call_env.map, param_name, param_value);

        arguments_count++;
        args_list = args_list->cdr;
    }

    if (arguments_count != user_function.params.count) {
        AnanasErrorContextMessage(error_ctx,
                                  where.row,
                                  where.col,
                                  "Not enough arguments: expected %zu, got %zu",
                                  user_function.params.count,
                                  arguments_count);
        return 0;
    }

    AnanasList *function_body = user_function.body;
    return AnanasEvalFormList(function_body,
                              arena,
                              &call_env,
                              error_ctx,
                              result);
}

B32 AnanasEvalMacroWithArgumentList(AnanasMacro *macro,
                                    AnanasToken where,
                                    AnanasList *args_list,
                                    AnanasArena *arena,
                                    AnanasEnv *env,
                                    AnanasErrorContext *error_ctx,
                                    AnanasValue *result) {
    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

    if (macro->is_native) {
        AnanasNativeMacro native_macro = macro->u.native;

        AnanasArgsArray args_array;
        AnanasArgsArrayInit(&args_array, arena_allocator, 10);

        while (args_list != NULL) {
            AnanasValue arg = args_list->car;
            AnanasArgsArrayPush(&args_array, arg);
            args_list = args_list->cdr;
        }

        AnanasArgs call_args = {.values = args_array.items, .count = args_array.count};

        return native_macro(call_args, where, arena, error_ctx, result);
    }

    AnanasUserMacro user_macro = macro->u.user;

    AnanasEnv call_env;
    AnanasEnvInit(&call_env, user_macro.enclosing_env, arena_allocator);

    UZ args_count = 0;
    while (args_list != NULL) {
        if (args_count >= user_macro.params.count) {
            AnanasErrorContextMessage(error_ctx,
                                      where.row,
                                      where.col,
                                      "too many arguments for macro call: expected %zu, got %zu instead",
                                      user_macro.params.count,
                                      args_count + 1);
            return 0;
        }

        HeliosStringView param_name = user_macro.params.names[args_count];
        AnanasEnvMapInsert(&call_env.map, param_name, args_list->car);

        ++args_count;
        args_list = args_list->cdr;
    }

    if (args_count != user_macro.params.count) {
        AnanasErrorContextMessage(error_ctx,
                                  where.row,
                                  where.col,
                                  "Not enough arguments for macro call: expected '%zu', got %zu instead",
                                  user_macro.params.count,
                                  args_count);
        return 0;
    }

    AnanasValue macro_result;
    if (!AnanasEvalFormList(user_macro.body,
                            arena,
                            &call_env,
                            error_ctx,
                            &macro_result)) return 1;
    return AnanasEval(macro_result, arena, env, result, error_ctx);
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
        AnanasValue param_node = params_list->car;
        if (param_node.type != AnanasValueType_Symbol) {
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

static const char *AnanasTypeName(AnanasValueType type) {
    switch (type) {
    case AnanasValueType_Int:      return "int";
    case AnanasValueType_String:   return "string";
    case AnanasValueType_Bool:     return "bool";
    case AnanasValueType_Function: return "function";
    case AnanasValueType_Macro:    return "macro";
    case AnanasValueType_List:     return "list";
    case AnanasValueType_Symbol:   return "symbol";
    }
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasCons) {
    ANANAS_CHECK_ARGS_COUNT(2);

    ANANAS_CHECK_ARG_TYPE(1, List, cdr);

    AnanasList *list = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
    list->car = args.values[0];
    list->cdr = cdr_arg.u.list;

    result->type = AnanasValueType_List;
    result->u.list = list;

    return 1;
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasListProc) {
    (void) where;
    (void) error_ctx;

    AnanasList *result_list = NULL;
    AnanasList *current_list = result_list;

    for (UZ i = 0; i < args.count; ++i) {
        AnanasList *list = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
        list->car = args.values[i];
        if (result_list == NULL) {
            result_list = list;
            current_list = list;
        } else {
            current_list->cdr = list;
            current_list = list;
        }
    }

    result->type = AnanasValueType_List;
    result->u.list = result_list;
    return 1;
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasCar) {
    (void) arena;
    ANANAS_CHECK_ARGS_COUNT(1);

    ANANAS_CHECK_ARG_TYPE(0, List, list);

    AnanasList *list = list_arg.u.list;
    if (list == NULL) {
        ANANAS_NATIVE_BAIL("called 'car' on an empty list");
    }

    ANANAS_NATIVE_RETURN(list->car);
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasCdr) {
    (void) arena;
    ANANAS_CHECK_ARGS_COUNT(1);

    ANANAS_CHECK_ARG_TYPE(0, List, list);

    AnanasList *list = list_arg.u.list;
    if (list == NULL) {
        ANANAS_NATIVE_BAIL("called 'cdr' on an empty list");
    }

    result->type = AnanasValueType_List;
    result->u.list = list->cdr;
    return 1;
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasReadFile) {
    ANANAS_CHECK_ARGS_COUNT(1);

    ANANAS_CHECK_ARG_TYPE(0, String, file_name);

    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

    HeliosStringView file_name = file_name_arg.u.string;
    HeliosStringView file_contents = HeliosReadEntireFile(arena_allocator, file_name);
    if (file_contents.data == NULL) {
        *result = ANANAS_FALSE;
        return 1;
    }

    result->type = AnanasValueType_String;
    result->u.string = file_contents;
    return 1;
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasStringSplit) {
    ANANAS_CHECK_ARGS_COUNT(2);

    ANANAS_CHECK_ARG_TYPE(0, String, string);
    ANANAS_CHECK_ARG_TYPE(1, String, separator);

    HeliosStringView string = string_arg.u.string;
    HeliosStringView separator = separator_arg.u.string;

    AnanasList *results_list = NULL;
    AnanasList *current_list = results_list;

    UZ substring_start = 0;
    for (SZ i = 0; i < (SZ)string.count - (SZ)separator.count + 1; ) {
        HeliosStringView s = {.data = &string.data[i], .count = separator.count};
        if (!HeliosStringViewEqual(separator, s)) {
            ++i;
            continue;
        }

        HeliosStringView string_part = {.data = &string.data[substring_start], .count = i - substring_start};

        AnanasList *list = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
        list->car.type = AnanasValueType_String;
        list->car.u.string = string_part;

        if (results_list == NULL) {
            results_list = list;
            current_list = list;
        } else {
            current_list->cdr = list;
            current_list = list;
        }

        substring_start = i + s.count;
        i += s.count;
    }

    HeliosStringView last_string_part = {.data = &string.data[substring_start], .count = string.count - substring_start};

    AnanasList *list = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
    list->car.type = AnanasValueType_String;
    list->car.u.string = last_string_part;

    if (results_list == NULL) {
        results_list = list;
    } else {
        current_list->cdr = list;
    }

    result->type = AnanasValueType_List;
    result->u.list = results_list;
    return 1;
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasConcat) {
    ANANAS_CHECK_ARGS_COUNT(2);

    ANANAS_CHECK_ARG_TYPE(0, String, lhs);
    ANANAS_CHECK_ARG_TYPE(1, String, rhs);

    HeliosStringView lhs = lhs_arg.u.string;
    HeliosStringView rhs = rhs_arg.u.string;

    UZ buf_count = lhs.count + rhs.count;

    U8 *buf = AnanasArenaPush(arena, buf_count);
    memcpy(buf, lhs.data, lhs.count);
    memcpy(&buf[lhs.count], rhs.data, rhs.count);

    result->type = AnanasValueType_String;
    result->u.string.data = buf;
    result->u.string.count = buf_count;
    return 1;
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasSubstring) {
    (void) arena;

    if (args.count < 2) {
        ANANAS_NATIVE_BAIL_FMT("expected at least 2 arguments, got %zu instead", args.count);
    }

    ANANAS_CHECK_ARG_TYPE(0, String, string);
    ANANAS_CHECK_ARG_TYPE(1, Int, start);

    HeliosStringView string = string_arg.u.string;
    UZ substring_start = start_arg.u.integer;

    if (substring_start >= string.count) {
        result->type = AnanasValueType_String;
        result->u.string = HELIOS_SV_LIT("");
        return 1;
    }

    UZ substring_count;

    if (args.count == 2) {
        substring_count = string.count - substring_start;
    } else if (args.count == 3) {
        ANANAS_CHECK_ARG_TYPE(2, Int, count);
        substring_count = count_arg.u.integer;

        UZ available_bytes = string.count - substring_start;
        if (available_bytes < substring_count) {
            ANANAS_NATIVE_BAIL_FMT("number of available bytes %zu in string is less than requested (%zu)", available_bytes, substring_count);
        }
    } else {
        HELIOS_UNREACHABLE();
    }

    result->type = AnanasValueType_String;
    result->u.string.data = &string.data[substring_start];
    result->u.string.count = substring_count;
    return 1;
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasPrintBuiltin) {
    ANANAS_CHECK_ARGS_COUNT(1);

    AnanasValue value = AnanasArgAt(args, 0);

    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);
    HeliosStringView string = AnanasPrint(arena_allocator, value);
    printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(string));
    ANANAS_NATIVE_RETURN(value);
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasRead) {
    ANANAS_CHECK_ARGS_COUNT(1);

    ANANAS_CHECK_ARG_TYPE(0, String, source);

    HeliosStringView source = source_arg.u.string;

    HeliosString8Stream source_stream;
    HeliosString8StreamInit(&source_stream, source.data, source.count);

    AnanasLexer lexer;
    AnanasLexerInit(&lexer, &source_stream);

    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

    AnanasReaderTable reader_table;
    AnanasReaderTableInit(&reader_table, arena_allocator);

    AnanasList *result_list = NULL;
    AnanasList *current_list = result_list;

    AnanasValue read_result;
    while (AnanasReaderNext(&lexer, &reader_table, arena, &read_result, error_ctx)) {
        AnanasList *list = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
        list->car = read_result;

        if (result_list == NULL) {
            result_list = list;
            current_list = list;
        } else {
            current_list->cdr = list;
            current_list = list;
        }
    }

    if (!error_ctx->ok) return 0;

    result->type = AnanasValueType_List;
    result->u.list = result_list;
    return 1;
}

static B32 AnanasEqual(AnanasValue lhs, AnanasValue rhs) {
    if (lhs.type != rhs.type) return 0;

    switch (lhs.type) {
    case AnanasValueType_Int: return lhs.u.integer == rhs.u.integer;
    case AnanasValueType_Bool: return lhs.u.boolean == rhs.u.boolean;
    case AnanasValueType_String: return HeliosStringViewEqual(lhs.u.string, rhs.u.string);
    case AnanasValueType_Function: return lhs.u.function == rhs.u.function;
    case AnanasValueType_Macro: return lhs.u.macro == rhs.u.macro;
    case AnanasValueType_Symbol: return HeliosStringViewEqual(lhs.u.symbol, rhs.u.symbol);
    case AnanasValueType_List: {
        AnanasList *lhs_list = lhs.u.list;
        AnanasList *rhs_list = rhs.u.list;

        while (lhs_list != NULL) {
            if (rhs_list == NULL) return 0;

            AnanasValue lhs_car = lhs_list->car;
            AnanasValue rhs_car = rhs_list->car;
            if (!AnanasEqual(lhs_car, rhs_car)) return 0;

            lhs_list = lhs_list->cdr;
            rhs_list = rhs_list->cdr;
        }

        return rhs_list == NULL;
    }
    }

    HELIOS_UNREACHABLE();
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasEqualBuiltin) {
    (void) arena;

    ANANAS_CHECK_ARGS_COUNT(2);

    AnanasValue lhs = AnanasArgAt(args, 0);
    AnanasValue rhs = AnanasArgAt(args, 1);

    result->type = AnanasValueType_Bool;
    result->u.boolean = AnanasEqual(lhs, rhs);
    return 1;
}

static B32 AnanasUnquoteForm(AnanasValue *value,
                             AnanasArena *arena,
                             AnanasEnv *env,
                             AnanasErrorContext *error_ctx) {
    if (value->type != AnanasValueType_List) return 1;

    AnanasList *list = value->u.list;
    AnanasList *current_list = list;

    while (current_list != NULL) {
        if (!AnanasUnquoteForm(&current_list->car, arena, env, error_ctx)) return 0;
        current_list = current_list->cdr;
    }

    if (list == NULL) return 1;
    if (list->car.type != AnanasValueType_Symbol) return 1;

    HeliosStringView car_symbol = list->car.u.symbol;
    if (!HeliosStringViewEqualCStr(car_symbol, "unquote")) return 1;

    AnanasList *unquote_args = list->cdr;
    if (unquote_args == NULL) {
        AnanasErrorContextMessage(error_ctx, value->token.row, value->token.col, "no argument passed to 'unquote' form");
        return 0;
    }

    if (unquote_args->cdr != NULL) {
        AnanasErrorContextMessage(error_ctx, value->token.row, value->token.col, "'unquote' form expects exactly one argument");
        return 0;
    }

    AnanasValue form = unquote_args->car;
    return AnanasEval(form, arena, env, value, error_ctx);
}

B32 AnanasEval(AnanasValue node, AnanasArena *arena, AnanasEnv *env, AnanasValue *result, AnanasErrorContext *error_ctx) {
    switch (node.type) {
    case AnanasValueType_Macro:
    case AnanasValueType_Function:
    case AnanasValueType_Bool:
    case AnanasValueType_String:
    case AnanasValueType_Int: {
        *result = node;
        return 1;
    }
    case AnanasValueType_Symbol: {
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
    case AnanasValueType_List: {
        AnanasList *list = node.u.list;
        if (list == NULL) {
            AnanasErrorContextMessage(error_ctx,
                                      node.token.row,
                                      node.token.col,
                                      "Cannot evaluate a nil list");
            return 0;
        }

        if (list->car.type != AnanasValueType_Symbol) {
            AnanasValue function_node;
            if (!AnanasEval(list->car, arena, env, &function_node, error_ctx)) return 0;

            if (function_node.type != AnanasValueType_Function) {
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

            if (var_name_cons->car.type != AnanasValueType_Symbol) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'var' name should be a symbol");
                return 0;
            }

            HeliosStringView var_name = var_name_cons->car.u.symbol;

            AnanasList *var_value_cons = var_name_cons->cdr;
            if (var_value_cons == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'var' should have a variable value");
                return 0;
            }

            AnanasValue var_value;
            if (!AnanasEval(var_value_cons->car, arena, env, &var_value, error_ctx)) return 0;

            AnanasEnvMapInsert(&env->map, var_name, var_value);

            *result = var_value;

            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "set")) {
            AnanasList *args_list = list->cdr;
            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no arguments passed to 'set'");
                return 0;
            }

            if (args_list->cdr == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no variable value passed to 'set'");
                return 0;
            }

            AnanasValue variable_name_value = args_list->car;
            if (variable_name_value.type != AnanasValueType_Symbol) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "'set' form expects the first argument to by a symbol, got %s instead",
                                          AnanasTypeName(variable_name_value.type));
                return 0;
            }

            HeliosStringView variable_name = variable_name_value.u.symbol;

            AnanasValue variable_value;
            if (!AnanasEnvLookup(env, variable_name, &variable_value)) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "symbol '" HELIOS_SV_FMT "' is not bound in this scope",
                                          HELIOS_SV_ARG(variable_name));
                return 0;
            }

            AnanasValue new_variable_value_given = args_list->cdr->car;
            AnanasValue new_variable_value;
            if (!AnanasEval(new_variable_value_given, arena, env, &new_variable_value, error_ctx)) return 0;

            AnanasEnvMapInsert(&env->map, variable_name, new_variable_value);
            *result = new_variable_value;
            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "if")) {
            AnanasList *args_list = list->cdr;

            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no arguments passed to 'if'");
                return 0;
            }

            if (args_list->cdr == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'if' form requires at least two arguments");
                return 0;
            }

            AnanasValue cond_value = args_list->car;
            AnanasValue cond;
            if (!AnanasEval(cond_value, arena, env, &cond, error_ctx)) return 0;

            AnanasValue branch_to_eval;

            if (AnanasConvertToBool(cond)) {
                AnanasList *cons_args = args_list->cdr;
                HELIOS_ASSERT(cons_args != NULL);
                branch_to_eval = cons_args->car;
            } else {
                AnanasList *alt_args = args_list->cdr->cdr;
                if (alt_args == NULL) {
                    *result = ANANAS_FALSE;
                    return 1;
                }

                branch_to_eval = alt_args->car;
            }

            return AnanasEval(branch_to_eval, arena, env, result, error_ctx);
        } else if (HeliosStringViewEqualCStr(sym_name, "lambda")) {
            AnanasList *lambda_params_cons = list->cdr;
            if (lambda_params_cons == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'lambda' should have an arg list");
                return 0;
            }

            if (lambda_params_cons->car.type != AnanasValueType_List) {
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

            AnanasUserFunction lambda = {
                .params = lambda_params,
                .body = lambda_body,
                .enclosing_env = env,
            };

            AnanasFunction *function = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasFunction);
            function->is_native = 0;
            function->u.user = lambda;

            result->type = AnanasValueType_Function;
            result->u.function = function;

            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "or")) {
            AnanasList *args_list = list->cdr;

            AnanasValue truthy_node = {.type = AnanasValueType_Int, .u = {.integer = 0}};

            while (args_list != NULL) {
                AnanasValue car;
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

            AnanasValue falsy_node = {.type = AnanasValueType_Int, .u = {.integer = 0}};

            while (args_list != NULL) {
                AnanasValue car;
                if (!AnanasEval(args_list->car, arena, env, &car, error_ctx)) return 0;

                falsy_node = car;

                if (!AnanasConvertToBool(car)) {
                    break;
                }

                args_list = args_list->cdr;
            }

            *result = falsy_node;
            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "do")) {
            AnanasList *args_list = list->cdr;

            AnanasValue do_result = ANANAS_FALSE;
            while (args_list != NULL) {
                AnanasValue form = args_list->car;
                if (!AnanasEval(form, arena, env, &do_result, error_ctx)) return 0;
                args_list = args_list->cdr;
            }

            *result = do_result;
            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "let")) {
            AnanasList *args_list = list->cdr;
            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "no bindings list passed to 'let' form");
                return 0;
            }

            AnanasValue bindings_value = args_list->car;
            if (bindings_value.type != AnanasValueType_List) {
                AnanasErrorContextMessage(error_ctx,
                                          bindings_value.token.row,
                                          bindings_value.token.col,
                                          "first argument to the 'let' form is not a list");
                return 0;
            }

            HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(arena);

            AnanasEnv let_env;
            AnanasEnvInit(&let_env, env, arena_allocator);
            AnanasList *bindings_list = bindings_value.u.list;

            while (bindings_list != NULL) {
                AnanasValue binding_pair_as_value = bindings_list->car;
                if (binding_pair_as_value.type != AnanasValueType_List) {
                    AnanasErrorContextMessage(error_ctx,
                                              binding_pair_as_value.token.row,
                                              binding_pair_as_value.token.col,
                                              "expected a list, got a value of type '%s' instead",
                                              AnanasTypeName(binding_pair_as_value.type));
                    return 0;
                }

                AnanasList *binding_pair = binding_pair_as_value.u.list;
                if (binding_pair == NULL) {
                    AnanasErrorContextMessage(error_ctx,
                                              binding_pair_as_value.token.row,
                                              binding_pair_as_value.token.col,
                                              "cannot use an empty list as a binding pair");
                    return 0;
                }

                if (binding_pair->cdr == NULL) {
                    AnanasErrorContextMessage(error_ctx,
                                              binding_pair_as_value.token.row,
                                              binding_pair_as_value.token.col,
                                              "missing a binding value in a binding pair");
                    return 0;
                }

                if (binding_pair->cdr->cdr != NULL) {
                    AnanasErrorContextMessage(error_ctx,
                                              binding_pair_as_value.token.row,
                                              binding_pair_as_value.token.col,
                                              "a binding pair is expected to have exactly 2 elements");
                    return 0;
                }

                AnanasValue binding_pair_name_value = binding_pair->car;
                if (binding_pair_name_value.type != AnanasValueType_Symbol) {
                    AnanasErrorContextMessage(error_ctx,
                                              binding_pair_name_value.token.row,
                                              binding_pair_name_value.token.col,
                                              "a name in a binding pair should be a symbol");
                    return 0;
                }

                HeliosStringView binding_pair_name = binding_pair_name_value.u.symbol;
                AnanasValue binding_pair_given_value = binding_pair->cdr->car;

                AnanasValue binding_pair_value;
                if (!AnanasEval(binding_pair_given_value, arena, &let_env, &binding_pair_value, error_ctx)) return 0;

                AnanasEnvMapInsert(&let_env.map, binding_pair_name, binding_pair_value);

                bindings_list = bindings_list->cdr;
            }

            AnanasList *forms_to_eval = args_list->cdr;
            return AnanasEvalFormList(forms_to_eval, arena, &let_env, error_ctx, result);
        } else if (HeliosStringViewEqualCStr(sym_name, "quote")) {
            AnanasList *args_list = list->cdr;
            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no argument passed to 'quote' form");
                return 0;
            }

            if (args_list->cdr != NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'quote' form expects exactly one argument");
                return 0;
            }

            AnanasValue form = args_list->car;
            if (!AnanasUnquoteForm(&form, arena, env, error_ctx)) return 0;
            *result = form;
            return 1;
        } else if (HeliosStringViewEqualCStr(sym_name, "unquote")) {
            AnanasList *args_list = list->cdr;
            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no argument passed to 'unquote' form");
                return 0;
            }

            if (args_list->cdr != NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'unquote' form expects exactly one argument");
                return 0;
            }

            AnanasValue given_value = args_list->car;
            return AnanasEval(given_value, arena, env, result, error_ctx);
        } else if (HeliosStringViewEqualCStr(sym_name, "macro")) {
            AnanasList *args_list = list->cdr;
            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no name passed to 'macro' form");
                return 0;
            }

            AnanasValue macro_name_node = args_list->car;
            if (macro_name_node.type != AnanasValueType_Symbol) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'macro' name should be a symbol");
                return 0;
            }

            HeliosStringView macro_name = macro_name_node.u.string;

            args_list = args_list->cdr;
            if (args_list == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no arguments passed to 'macro' form");
                return 0;
            }

            AnanasValue macro_args_node = args_list->car;
            if (macro_args_node.type != AnanasValueType_List) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "'macro' form arguments should be a list");
                return 0;
            }

            AnanasList *macro_params_list = macro_args_node.u.list;
            AnanasParams macro_params;
            if (!AnanasParseParamsFromList(arena, macro_params_list, &macro_params, error_ctx)) return 0;

            AnanasList *macro_body = args_list->cdr;

            AnanasUserMacro user_macro = {
                .body = macro_body,
                .enclosing_env = env,
                .params = macro_params,
            };
            AnanasMacro *macro = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasMacro);
            macro->is_native = 0;
            macro->u.user = user_macro;

            AnanasValue macro_node = {
                .type = AnanasValueType_Macro,
                .u = {
                    .macro = macro,
                },
            };

            AnanasEnvMapInsert(&env->map, macro_name, macro_node);
            *result = macro_node;
            return 1;
        } else {
            AnanasValue callable_node;
            if (!AnanasEnvLookup(env, sym_name, &callable_node)) {
                AnanasToken token = list->car.token;
                AnanasErrorContextMessage(error_ctx,
                                          token.row,
                                          token.col,
                                          "Unbound symbol '" HELIOS_SV_FMT "'",
                                          HELIOS_SV_ARG(sym_name));
                return 0;
            }

            if (callable_node.type == AnanasValueType_Function) {
                AnanasFunction *function = callable_node.u.function;
                return AnanasEvalFunctionWithArgumentList(function,
                                                          list->cdr,
                                                          node.token,
                                                          arena,
                                                          env,
                                                          error_ctx,
                                                          result);
            } else if (callable_node.type == AnanasValueType_Macro) {
                AnanasMacro *macro = callable_node.u.macro;
                return AnanasEvalMacroWithArgumentList(macro,
                                                       node.token,
                                                       list->cdr,
                                                       arena,
                                                       env,
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
