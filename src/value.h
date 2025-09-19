#ifndef ANANAS_VALUE_H_
#define ANANAS_VALUE_H_

#include "astron.h"
#include "common.h"
#include "lexer.h"

typedef enum {
    AnanasValueType_String,
    AnanasValueType_Int,
    AnanasValueType_Bool,
    AnanasValueType_Symbol,
    AnanasValueType_List,
    AnanasValueType_Function,
    AnanasValueType_Macro,
} AnanasValueType;

struct AnanasList;
typedef struct AnanasList AnanasList;

struct AnanasFunction;
typedef struct AnanasFunction AnanasFunction;

struct AnanasMacro;
typedef struct AnanasMacro AnanasMacro;

typedef struct {
    AnanasValueType type;
    AnanasToken token;
    union {
        HeliosStringView string;
        S64 integer;
        B32 boolean;
        HeliosStringView symbol;
        AnanasList *list;
        AnanasFunction *function;
        AnanasMacro *macro;
    } u;
} AnanasValue;

struct AnanasList {
    AnanasValue car;
    struct AnanasList *cdr;
};

static inline AnanasList *AnanasListCopy(AnanasArena *arena, AnanasList *list) {
    AnanasList *out_list = NULL;
    AnanasList *current_out_list = out_list;

    while (list != NULL) {
        AnanasList *l = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);

        if (list->car.type == AnanasValueType_List) {
            AnanasList *list_to_copy = list->car.u.list;
            l->car.type = AnanasValueType_List;
            l->car.u.list = AnanasListCopy(arena, list_to_copy);
        } else {
            l->car = list->car;
        }

        if (out_list == NULL) {
            out_list = l;
            current_out_list = l;
        } else {
            current_out_list->cdr = l;
            current_out_list = l;
        }

        list = list->cdr;
    }

    return out_list;
}

struct AnanasEnv;

typedef struct {
    HeliosStringView *names;
    B32 variable;
    UZ count;
} AnanasParams;

B32 AnanasParseParamsFromList(HeliosAllocator,
                              AnanasList *,
                              AnanasParams *,
                              AnanasErrorContext *);

B32 AnanasUnquoteForm(AnanasList *,
                      AnanasArena *,
                      struct AnanasEnv *,
                      AnanasErrorContext *);

typedef struct {
    AnanasValue *values;
    UZ count;
} AnanasArgs;

static inline AnanasValue AnanasArgAt(AnanasArgs args, UZ idx) {
    HELIOS_VERIFY(args.count > idx);
    return args.values[idx];
}

ERMIS_DECL_ARRAY(AnanasValue, AnanasValueArray)

#define ANANAS_DECLARE_NATIVE_FUNCTION(name) B32 name(AnanasArgs args, \
    AnanasToken where, \
    AnanasArena *arena, \
    AnanasErrorContext *error_ctx, \
    AnanasValue *result)
#define ANANAS_DEFINE_NATIVE_FUNCTION ANANAS_DECLARE_NATIVE_FUNCTION

#define ANANAS_DECLARE_NATIVE_MACRO ANANAS_DECLARE_NATIVE_FUNCTION
#define ANANAS_DEFINE_NATIVE_MACRO ANANAS_DECLARE_NATIVE_MACRO

#define ANANAS_NATIVE_BAIL_FMT(fmt, ...) do {                          \
        AnanasErrorContextMessage(error_ctx,                            \
                                  where.row,                            \
                                  where.col,                            \
                                  fmt,                                  \
                                  __VA_ARGS__);                         \
        return 0;                                                       \
    } while (0)

#define ANANAS_NATIVE_BAIL(msg)  do {          \
        AnanasErrorContextMessage(error_ctx,    \
                                  where.row,    \
                                  where.col,    \
                                  msg);         \
        return 0;                               \
    } while (0)

#define ANANAS_CHECK_ARGS_COUNT(n) do {                                 \
        if (args.count != (n)) {                                        \
            ANANAS_NATIVE_BAIL_FMT("Argument count mismatch: expected %d but got %zu instead", \
                                    (n),                                \
                                    args.count);                        \
        }                                                               \
    } while (0)

#define ANANAS_CHECK_ARG_TYPE(n, arg_type, name)                        \
    AnanasValue name##_arg = AnanasArgAt(args, (n));                    \
    if (name##_arg.type != AnanasValueType_##arg_type) {                \
        ANANAS_NATIVE_BAIL_FMT("Argument type mismatch: expected the '" #name "' argument at position %d to be of type %s but got type %s instead", \
                                (n),                                    \
                                AnanasTypeName(AnanasValueType_##arg_type), \
                                AnanasTypeName(name##_arg.type));       \
    }

#define ANANAS_NATIVE_RETURN(value) do { *result = (value); return 1; } while (0)

typedef B32 (*AnanasNativeFunction)(AnanasArgs args,
                                    AnanasToken where,
                                    AnanasArena *arena,
                                    AnanasErrorContext *error_ctx,
                                    AnanasValue *result);

typedef struct {
    AnanasParams params;
    AnanasList *body;
    struct AnanasEnv *enclosing_env;
} AnanasUserFunction;

struct AnanasFunction {
    B32 is_native;
    union {
        AnanasNativeFunction native;
        AnanasUserFunction user;
    } u;
};

typedef AnanasNativeFunction AnanasNativeMacro;

typedef struct {
    AnanasParams params;
    AnanasList *body;
    struct AnanasEnv *enclosing_env;
} AnanasUserMacro;

struct AnanasMacro {
    B32 is_native;
    union {
        AnanasNativeMacro native;
        AnanasUserMacro user;
    } u;
};

#define ANANAS_BOOL(value) ((AnanasValue) {.type = AnanasValueType_Bool, .u = {.boolean = (value)}})
#define ANANAS_FALSE ANANAS_BOOL(0)
#define ANANAS_TRUE ANANAS_BOOL(1)

#endif // ANANAS_VALUE_H_
