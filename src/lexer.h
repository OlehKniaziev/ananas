#ifndef ANANAS_LEXER_H_
#define ANANAS_LEXER_H_

#include "astron.h"

#define ANANAS_ENUM_TOKEN_TYPES \
    X(LeftParen) \
    X(RightParen) \
    X(Int) \
    X(String) \
    X(UnclosedString) \
    X(Illegal) \
    X(Symbol) \
    X(ReaderMacro)

typedef enum {
#define X(t) AnanasTokenType_##t,
ANANAS_ENUM_TOKEN_TYPES
#undef X
} AnanasTokenType;

typedef struct {
    AnanasTokenType type;
    HeliosStringView value;
    U32 row;
    U32 col;
} AnanasToken;

typedef struct {
    HeliosString8Stream *contents;
    U32 row;
    U32 col;
} AnanasLexer;

void AnanasLexerInit(AnanasLexer *lexer, HeliosString8Stream *contents);

B32 AnanasLexerNext(AnanasLexer *lexer, AnanasToken *token);

#endif // ANANAS_LEXER_H_
