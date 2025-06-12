#ifndef ANANAS_LEXER_H_
#define ANANAS_LEXER_H_

#include "astron.h"

typedef enum {
    AnanasTokenType_LeftParen,
    AnanasTokenType_RightParen,
    AnanasTokenType_Int,
    AnanasTokenType_String,
    AnanasTokenType_UnclosedString,
    AnanasTokenType_Illegal,
    AnanasTokenType_Symbol,
    AnanasTokenType_ReaderMacro,
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
