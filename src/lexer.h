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
} AnanasToken;

B32 AnanasLexerNext(HeliosString8Stream *contents, AnanasToken *token);

#endif // ANANAS_LEXER_H_
