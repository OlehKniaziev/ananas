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
} AnanasASTNodeType;

struct AnanasList;
typedef struct AnanasList AnanasList;

struct AnanasFunction;
typedef struct AnanasFunction AnanasFunction;

typedef struct {
    AnanasASTNodeType type;
    union {
        HeliosStringView string;
        S64 integer;
        HeliosStringView symbol;
        AnanasList *list;
        AnanasFunction *function;
    } u;
} AnanasASTNode;

struct AnanasList {
    struct AnanasList *cdr;
    AnanasASTNode car;
};

struct AnanasEnv;

struct AnanasFunction {
    HeliosStringView *params_names;
    UZ params_count;
    AnanasASTNode body;
    struct AnanasEnv *enclosing_env;
};

B32 AnanasReaderNext(HeliosString8Stream *stream, AnanasArena *arena, AnanasASTNode *node);

#endif // ANANAS_READER_H_
