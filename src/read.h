#ifndef ANANAS_READER_H_
#define ANANAS_READER_H_

#include "astron.h"
#include "common.h"
#include "lexer.h"

typedef enum {
    AnanasASTNodeType_String,
    AnanasASTNodeType_Int,
    AnanasASTNodeType_Symbol,
    AnanasASTNodeType_List,
    AnanasASTNodeType_Function,
    AnanasASTNodeType_Macro,
} AnanasASTNodeType;

struct AnanasList;
typedef struct AnanasList AnanasList;

struct AnanasFunction;
typedef struct AnanasFunction AnanasFunction;

struct AnanasMacro;
typedef struct AnanasMacro AnanasMacro;

typedef struct {
    AnanasASTNodeType type;
    AnanasToken token;
    union {
        HeliosStringView string;
        S64 integer;
        HeliosStringView symbol;
        AnanasList *list;
        AnanasFunction *function;
        AnanasMacro *macro;
    } u;
} AnanasASTNode;

struct AnanasList {
    struct AnanasList *cdr;
    AnanasASTNode car;
};

struct AnanasEnv;

typedef struct {
    HeliosStringView *names;
    UZ count;
} AnanasParams;

struct AnanasFunction {
    AnanasParams params;
    AnanasList *body;
    struct AnanasEnv *enclosing_env;
};

struct AnanasMacro {
    AnanasParams params;
    AnanasList *body;
    struct AnanasEnv *enclosing_env;
};

B32 AnanasReaderNext(AnanasLexer *lexer, AnanasArena *arena, AnanasASTNode *node, AnanasErrorContext *error_ctx);

#endif // ANANAS_READER_H_
