#ifndef ANANAS_READER_H_
#define ANANAS_READER_H_

#include "astron.h"
#include "common.h"
#include "lexer.h"

typedef enum {
    AnanasValueType_String,
    AnanasValueType_Int,
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

struct AnanasEnv;

typedef struct {
    HeliosStringView *names;
    UZ count;
} AnanasParams;

typedef struct {
    AnanasValue *values;
    UZ count;
} AnanasArgs;

static inline AnanasValue AnanasArgAt(AnanasArgs args, UZ idx) {
    HELIOS_VERIFY(args.count > idx);
    return args.values[idx];
}

#define ANANAS_DECLARE_NATIVE_FUNCTION(name) B32 name(AnanasArgs args, \
    AnanasToken where, \
    AnanasArena *arena, \
    AnanasErrorContext *error_ctx, \
    AnanasValue *result)
#define ANANAS_DEFINE_NATIVE_FUNCTION ANANAS_DECLARE_NATIVE_FUNCTION

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

struct AnanasMacro {
    AnanasParams params;
    AnanasList *body;
    struct AnanasEnv *enclosing_env;
};

B32 AnanasReaderNext(AnanasLexer *lexer, AnanasArena *arena, AnanasValue *node, AnanasErrorContext *error_ctx);

#endif // ANANAS_READER_H_
