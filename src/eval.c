#include "eval.h"
#include "print.h"

ERMIS_IMPL_HASHMAP(HeliosStringView, AnanasValue, AnanasEnvMap, HeliosStringViewEqual, AnanasFnv1Hash)

static AnanasValue *AnanasEnvLookup(AnanasEnv *env, HeliosStringView name) {
    while (env != NULL) {
        AnanasValue *ptr = AnanasEnvMapFindPtr(&env->map, name);
        if (ptr != NULL) return ptr;
        env = env->parent_env;
    }

    return NULL;
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
    X("concat-syms", AnanasConcatSyms) \
    X("substring", AnanasSubstring) \
    X("print", AnanasPrintBuiltin) \
    X("print-string", AnanasPrintString) \
    X("read", AnanasRead) \
    X("=", AnanasEqualBuiltin) \
    X("type", AnanasType) \
    X("+", AnanasPlus) \
    X("-", AnanasMinus) \
    X("*", AnanasStar) \
    X("rem", AnanasRem) \
    X("to-string", AnanasToString) \
    X("error", AnanasError)

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
                              HeliosAllocator allocator,
                              AnanasEnv *env,
                              AnanasErrorContext *error_ctx,
                              AnanasValue *result) {
    HELIOS_VERIFY(form_list != NULL);

    while (form_list != NULL) {
        if (!AnanasEval(form_list->car, allocator, env, result, error_ctx)) return 0;
        form_list = form_list->cdr;
    }

    return 1;
}

static B32 AnanasEvalFunctionWithArgumentList(AnanasFunction *function,
                                              AnanasList *args_list,
                                              AnanasToken where,
                                              HeliosAllocator allocator,
                                              AnanasEnv *env,
                                              AnanasErrorContext *error_ctx,
                                              AnanasValue *result) {
    if (function->is_native) {
        AnanasValueArray args_array;
        AnanasValueArrayInit(&args_array, allocator, 10);

        while (args_list != NULL) {
            AnanasValue arg_value;
            if (!AnanasEval(args_list->car, allocator, env, &arg_value, error_ctx)) return 0;

            AnanasValueArrayPush(&args_array, arg_value);

            args_list = args_list->cdr;
        }

        AnanasArgs call_args = {
            .values = args_array.items,
            .count = args_array.count,
        };

        AnanasNativeFunction native_function = function->u.native;
        return native_function(call_args, where, allocator, error_ctx, result);
    }

    AnanasUserFunction user_function = function->u.user;

    AnanasEnv call_env;
    AnanasEnvInit(&call_env, user_function.enclosing_env, allocator);

    if (user_function.params.variable) {
        HELIOS_ASSERT(user_function.params.count >= 1);

        UZ expected_args_count = user_function.params.count - 1;

        UZ args_count = 0;
        while (args_list != NULL) {
            if (args_count >= expected_args_count) break;

            HeliosStringView param_name = user_function.params.names[args_count];
            AnanasValue param_value;
            if (!AnanasEval(args_list->car, allocator, env, &param_value, error_ctx)) return 0;
            AnanasEnvMapInsert(&call_env.map, param_name, param_value);

            ++args_count;
            args_list = args_list->cdr;
        }

        if (args_count < expected_args_count) {
            AnanasErrorContextMessage(error_ctx,
                                      where.row,
                                      where.col,
                                      "not enough arguments for function call: expected at least %zu, got %zu instead",
                                      expected_args_count,
                                      args_count);
            return 0;
        }

        HELIOS_ASSERT(user_function.params.count - args_count == 1);

        AnanasList *rest_list = NULL;
        AnanasList *current_rest_list = rest_list;

        while (args_list != NULL) {
            AnanasValue param_value;
            if (!AnanasEval(args_list->car, allocator, env, &param_value, error_ctx)) return 0;

            AnanasList *list = HeliosAlloc(allocator, sizeof(*list));
            list->car = param_value;

            if (rest_list == NULL) {
                rest_list = list;
                current_rest_list = list;
            } else {
                current_rest_list->cdr = list;
                current_rest_list = list;
            }

            args_list = args_list->cdr;
        }

        HeliosStringView rest_param_name = user_function.params.names[user_function.params.count - 1];
        AnanasValue rest_param_value = {.type = AnanasValueType_List, .u = {.list = rest_list}};
        AnanasEnvMapInsert(&call_env.map, rest_param_name, rest_param_value);
    } else {
        UZ arguments_count = 0;

        while (args_list != NULL) {
            if (arguments_count >= user_function.params.count) {
                AnanasErrorContextMessage(error_ctx,
                                          where.row,
                                          where.col,
                                          "too many arguments: expected %zu, got %zu",
                                          user_function.params.count,
                                          arguments_count + 1);
                return 0;
            }

            AnanasValue param_value;
            if (!AnanasEval(args_list->car, allocator, env, &param_value, error_ctx)) return 0;

            HeliosStringView param_name = user_function.params.names[arguments_count];
            AnanasEnvMapInsert(&call_env.map, param_name, param_value);

            arguments_count++;
            args_list = args_list->cdr;
        }

        if (arguments_count != user_function.params.count) {
            AnanasErrorContextMessage(error_ctx,
                                      where.row,
                                      where.col,
                                      "not enough arguments: expected %zu, got %zu",
                                      user_function.params.count,
                                      arguments_count);
            return 0;
        }
    }

    AnanasList *function_body = user_function.body;
    return AnanasEvalFormList(function_body,
                              allocator,
                              &call_env,
                              error_ctx,
                              result);
}

B32 AnanasEvalMacroWithArgumentList(AnanasMacro *macro,
                                    AnanasToken where,
                                    AnanasList *args_list,
                                    HeliosAllocator allocator,
                                    AnanasErrorContext *error_ctx,
                                    AnanasValue *result) {
    if (macro->is_native) {
        AnanasNativeMacro native_macro = macro->u.native;

        AnanasValueArray args_array;
        AnanasValueArrayInit(&args_array, allocator, 10);

        while (args_list != NULL) {
            AnanasValue arg = args_list->car;
            AnanasValueArrayPush(&args_array, arg);
            args_list = args_list->cdr;
        }

        AnanasArgs call_args = {.values = args_array.items, .count = args_array.count};

        return native_macro(call_args, where, allocator, error_ctx, result);
    }

    AnanasUserMacro user_macro = macro->u.user;

    AnanasEnv call_env;
    AnanasEnvInit(&call_env, user_macro.enclosing_env, allocator);

    if (user_macro.params.variable) {
        HELIOS_ASSERT(user_macro.params.count >= 1);

        UZ expected_args_count = user_macro.params.count - 1;

        UZ args_count = 0;
        while (args_list != NULL) {
            if (args_count >= expected_args_count) break;

            HeliosStringView param_name = user_macro.params.names[args_count];
            AnanasEnvMapInsert(&call_env.map, param_name, args_list->car);

            ++args_count;
            args_list = args_list->cdr;
        }

        if (args_count < expected_args_count) {
            AnanasErrorContextMessage(error_ctx,
                                      where.row,
                                      where.col,
                                      "not enough arguments for macro call: expected at least %zu, got %zu instead",
                                      expected_args_count,
                                      args_count);
            return 0;
        }

        HELIOS_ASSERT(user_macro.params.count - args_count == 1);

        HeliosStringView rest_param_name = user_macro.params.names[user_macro.params.count - 1];
        AnanasValue rest_param_value = {.type = AnanasValueType_List, .u = {.list = args_list}};
        AnanasEnvMapInsert(&call_env.map, rest_param_name, rest_param_value);
    } else {
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
    }

    return AnanasEvalFormList(user_macro.body,
                              allocator,
                              &call_env,
                              error_ctx,
                              result);
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

    AnanasList *list = HeliosAlloc(arena, sizeof(*list));
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
        AnanasList *list = HeliosAlloc(arena, sizeof(*list));
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

    HeliosStringView file_name = file_name_arg.u.string;
    HeliosStringView file_contents = HeliosReadEntireFile(arena, file_name);
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

        AnanasList *list = HeliosAlloc(arena, sizeof(*list));
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

    AnanasList *list = HeliosAlloc(arena, sizeof(*list));
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
    if (args.count == 0) {
        ANANAS_NATIVE_BAIL("no arguments passed to 'concat'");
    }

    AnanasDString buf = {0};
    AnanasDStringInit(&buf, arena, 1024);

    for (UZ i = 0; i < args.count; ++i) {
        AnanasValue arg = AnanasArgAt(args, i);
        if (arg.type != AnanasValueType_String) {
            AnanasErrorContextMessage(error_ctx,
                                      arg.token.row,
                                      arg.token.col,
                                      "expected a value of type string, got %s instead",
                                      AnanasTypeName(arg.type));
            return 0;
        }

        HeliosStringView sym = arg.u.string;
        for (UZ j = 0; j < sym.count; ++j) {
            AnanasDStringPush(&buf, sym.data[j]);
        }
    }

    result->type = AnanasValueType_String;
    result->u.symbol.data = buf.items;
    result->u.symbol.count = buf.count;
    return 1;
}

ANANAS_DEFINE_NATIVE_FUNCTION(AnanasConcatSyms) {
    if (args.count == 0) {
        ANANAS_NATIVE_BAIL("no arguments passed to 'concat-syms'");
    }

    AnanasDString buf = {0};
    AnanasDStringInit(&buf, arena, 1024);

    for (UZ i = 0; i < args.count; ++i) {
        AnanasValue arg = AnanasArgAt(args, i);
        if (arg.type != AnanasValueType_Symbol) {
            AnanasErrorContextMessage(error_ctx,
                                      arg.token.row,
                                      arg.token.col,
                                      "expected a value of type symbol, got %s instead",
                                      AnanasTypeName(arg.type));
            return 0;
        }

        HeliosStringView sym = arg.u.symbol;
        for (UZ j = 0; j < sym.count; ++j) {
            AnanasDStringPush(&buf, sym.data[j]);
        }
    }

    result->type = AnanasValueType_Symbol;
    result->u.symbol.data = buf.items;
    result->u.symbol.count = buf.count;
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

    HeliosStringView string = AnanasPrint(arena, value);
    printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(string));
    ANANAS_NATIVE_RETURN(value);
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasPrintString) {
    (void) arena;

    ANANAS_CHECK_ARGS_COUNT(1);

    ANANAS_CHECK_ARG_TYPE(0, String, s);

    HeliosStringView s = s_arg.u.string;
    printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(s));
    ANANAS_NATIVE_RETURN(s_arg);
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasRead) {
    ANANAS_CHECK_ARGS_COUNT(1);

    ANANAS_CHECK_ARG_TYPE(0, String, source);

    HeliosStringView source = source_arg.u.string;

    HeliosString8Stream source_stream;
    HeliosString8StreamInit(&source_stream, source.data, source.count);

    AnanasLexer lexer;
    AnanasLexerInit(&lexer, &source_stream);

    AnanasReaderTable reader_table;
    AnanasReaderTableInit(&reader_table, arena);

    AnanasList *result_list = NULL;
    AnanasList *current_list = result_list;

    AnanasValue read_result;
    while (AnanasReaderNext(&lexer, &reader_table, arena, &read_result, error_ctx)) {
        AnanasList *list = HeliosAlloc(arena, sizeof(*list));
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

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasType) {
    (void) arena;

    ANANAS_CHECK_ARGS_COUNT(1);

    AnanasValue arg = AnanasArgAt(args, 0);
    const char *type_name = AnanasTypeName(arg.type);
    result->type = AnanasValueType_Symbol;
    result->u.symbol = HELIOS_SV_LIT(type_name);
    return 1;
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasPlus) {
    (void) arena;

    ANANAS_CHECK_ARGS_COUNT(2);

    ANANAS_CHECK_ARG_TYPE(0, Int, lhs);
    ANANAS_CHECK_ARG_TYPE(1, Int, rhs);

    S64 lhs = lhs_arg.u.integer;
    S64 rhs = rhs_arg.u.integer;

    result->type = AnanasValueType_Int;
    result->u.integer = lhs + rhs;
    return 1;
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasMinus) {
    (void) arena;

    ANANAS_CHECK_ARGS_COUNT(2);

    ANANAS_CHECK_ARG_TYPE(0, Int, lhs);
    ANANAS_CHECK_ARG_TYPE(1, Int, rhs);

    S64 lhs = lhs_arg.u.integer;
    S64 rhs = rhs_arg.u.integer;

    result->type = AnanasValueType_Int;
    result->u.integer = lhs - rhs;
    return 1;
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasStar) {
    (void) arena;

    ANANAS_CHECK_ARGS_COUNT(2);

    ANANAS_CHECK_ARG_TYPE(0, Int, lhs);
    ANANAS_CHECK_ARG_TYPE(1, Int, rhs);

    S64 lhs = lhs_arg.u.integer;
    S64 rhs = rhs_arg.u.integer;

    result->type = AnanasValueType_Int;
    result->u.integer = lhs * rhs;
    return 1;
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasRem) {
    (void) arena;

    ANANAS_CHECK_ARGS_COUNT(2);

    ANANAS_CHECK_ARG_TYPE(0, Int, lhs);
    ANANAS_CHECK_ARG_TYPE(1, Int, rhs);

    S64 lhs = lhs_arg.u.integer;
    S64 rhs = rhs_arg.u.integer;

    result->type = AnanasValueType_Int;
    result->u.integer = lhs % rhs;
    return 1;
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasToString) {
    ANANAS_CHECK_ARGS_COUNT(1);

    AnanasValue arg = AnanasArgAt(args, 0);

    if (arg.type == AnanasValueType_String) {
        ANANAS_NATIVE_RETURN(arg);
    }

    HeliosStringView s = AnanasPrint(arena, arg);
    result->type = AnanasValueType_String;
    result->u.string = s;
    return 1;
}

ANANAS_DECLARE_NATIVE_FUNCTION(AnanasError) {
    (void) arena;

    ANANAS_CHECK_ARGS_COUNT(1);
    ANANAS_CHECK_ARG_TYPE(0, String, msg);

    HeliosStringView msg = msg_arg.u.string;
    *result = ANANAS_FALSE;
    ANANAS_NATIVE_BAIL_FMT(HELIOS_SV_FMT, HELIOS_SV_ARG(msg));
}

B32 AnanasEval(AnanasValue node, HeliosAllocator arena, AnanasEnv *env, AnanasValue *result, AnanasErrorContext *error_ctx) {
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
        AnanasValue *symbol_value = AnanasEnvLookup(env, node.u.symbol);
        if (symbol_value == NULL) {
            AnanasErrorContextMessage(error_ctx,
                                      node.token.row,
                                      node.token.col,
                                      "unbound symbol '" HELIOS_SV_FMT "'",
                                      HELIOS_SV_ARG(node.u.symbol));
            return 0;
        }

        *result = *symbol_value;
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

            AnanasValue *variable_value = AnanasEnvLookup(env, variable_name);
            if (variable_value == NULL) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "symbol '" HELIOS_SV_FMT "' is not bound in this scope",
                                          HELIOS_SV_ARG(variable_name));
                return 0;
            }

            AnanasValue new_variable_value_given = args_list->cdr->car;
            // NOTE(oleh): Could just pass the `variable_value` pointer to the eval call ahead, but
            // i don't want to make any guarantees about not modifying the result parameter on error.
            AnanasValue new_variable_value;
            if (!AnanasEval(new_variable_value_given, arena, env, &new_variable_value, error_ctx)) return 0;
            *variable_value = new_variable_value;
            *result = *variable_value;
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

            AnanasFunction *function = HeliosAlloc(arena, sizeof(*function));
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

            if (args_list->cdr == NULL) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "no expressions to evaluate passed to 'let' form");
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

            AnanasEnv let_env;
            AnanasEnvInit(&let_env, env, arena);
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

            AnanasList *unquote_args = AnanasListCopy(arena, args_list);

            if (!AnanasUnquoteForm(unquote_args, arena, env, error_ctx)) return 0;

            if (unquote_args->cdr == NULL) {
                *result = unquote_args->car;
            } else {
                result->type = AnanasValueType_List;
                result->u.list = unquote_args;
            }

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
            AnanasMacro *macro = HeliosAlloc(arena, sizeof(*macro));
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
        } else if (HeliosStringViewEqualCStr(sym_name, "macroexpand")) {
            AnanasList *args = list->cdr;
            if (args == NULL) {
                AnanasErrorContextMessage(error_ctx, node.token.row, node.token.col, "no argument passed to 'macroexpand' form");
                return 0;
            }

            AnanasValue macro_list_value;
            if (!AnanasEval(args->car, arena, env, &macro_list_value, error_ctx)) return 0;

            if (macro_list_value.type != AnanasValueType_List) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "'macroexpand' form expects a list as it's argument, but got %s instead",
                                          AnanasTypeName(macro_list_value.type));
                return 0;
            }

            AnanasList *macro_list = macro_list_value.u.list;

            if (macro_list == NULL) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "cannot call 'macroexpand' with an empty list");
                return 0;
            }

            AnanasValue macro_value;
            if (!AnanasEval(macro_list->car, arena, env, &macro_value, error_ctx)) return 0;

            if (macro_value.type != AnanasValueType_Macro) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "'macroexpand' form expects the car of the list argument to be a macro, but it is of type %s",
                                          AnanasTypeName(args->car.type));
                return 0;
            }

            AnanasMacro *macro = macro_value.u.macro;
            AnanasList *macro_args = macro_list->cdr;
            return AnanasEvalMacroWithArgumentList(macro,
                                                   node.token,
                                                   macro_args,
                                                   arena,
                                                   error_ctx,
                                                   result);
        } else if (HeliosStringViewEqualCStr(sym_name, "apply")) {
            AnanasList *apply_args = list->cdr;

            if (apply_args == NULL) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "no arguments passed to 'apply'");
                return 0;
            }

            AnanasValue fn_arg_value = apply_args->car;
            AnanasValue fn_arg;
            if (!AnanasEval(fn_arg_value, arena, env, &fn_arg, error_ctx)) return 0;

            if (fn_arg.type != AnanasValueType_Function) {
                AnanasErrorContextMessage(error_ctx,
                                          node.token.row,
                                          node.token.col,
                                          "expected the argument at position 0 to be of type function, got a value of type %s instead",
                                          AnanasTypeName(fn_arg.type));
                return 0;
            }

            AnanasFunction *function = fn_arg.u.function;

            AnanasList *args_list = NULL;
            AnanasList *current_args_list = NULL;

            apply_args = apply_args->cdr;

#define APPEND(val) do { \
            AnanasList *car = HeliosAlloc(arena, sizeof(*car)); \
    car->car = (val); \
    if (args_list == NULL) { \
        args_list = (car); \
        current_args_list = (car); \
    } else { \
        current_args_list->cdr = (car); \
        current_args_list = (car); \
    } \
} while (0)

            while (apply_args != NULL && apply_args->cdr != NULL) {
                AnanasValue arg = apply_args->car;
                APPEND(arg);
                apply_args = apply_args->cdr;
            }

            if (apply_args != NULL) {
                AnanasValue last_arg_value = apply_args->car;
                AnanasValue last_arg;
                if (!AnanasEval(last_arg_value, arena, env, &last_arg, error_ctx)) return 0;

                if (last_arg.type != AnanasValueType_List) {
                    AnanasErrorContextMessage(error_ctx,
                                              node.token.row,
                                              node.token.col,
                                              "expected the last argument passed to 'apply' to be of type list, got a value of type %s instead",
                                           AnanasTypeName(last_arg.type));
                    return 0;
                }

                AnanasList *list = last_arg.u.list;
                while (list != NULL) {
                    APPEND(list->car);
                    list = list->cdr;
                }
            }

            #undef APPEND

            return AnanasEvalFunctionWithArgumentList(function,
                                                      args_list,
                                                      node.token,
                                                      arena,
                                                      env,
                                                      error_ctx,
                                                      result);
        } else {
            AnanasValue *callable_node = AnanasEnvLookup(env, sym_name);
            if (callable_node == NULL) {
                AnanasToken token = list->car.token;
                AnanasErrorContextMessage(error_ctx,
                                          token.row,
                                          token.col,
                                          "unbound symbol '" HELIOS_SV_FMT "'",
                                          HELIOS_SV_ARG(sym_name));
                return 0;
            }

            if (callable_node->type == AnanasValueType_Function) {
                AnanasFunction *function = callable_node->u.function;
                return AnanasEvalFunctionWithArgumentList(function,
                                                          list->cdr,
                                                          node.token,
                                                          arena,
                                                          env,
                                                          error_ctx,
                                                          result);
            } else if (callable_node->type == AnanasValueType_Macro) {
                AnanasMacro *macro = callable_node->u.macro;
                AnanasValue macro_result;
                if (!AnanasEvalMacroWithArgumentList(macro,
                                                     node.token,
                                                     list->cdr,
                                                     arena,
                                                     error_ctx,
                                                     &macro_result)) return 0;
                B32 res = AnanasEval(macro_result, arena, env, result, error_ctx);
                return res;
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
