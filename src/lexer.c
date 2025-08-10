#include "lexer.h"

static B32 AnanasIsReaderMacroChar(HeliosChar c) {
    return c == '@' ||
           c == ',' ||
           c == '.' ||
           c == '#' ||
           c == '~' ||
           c == '\'';
}

static B32 AnanasIsSymbolChar(HeliosChar c) {
    return HeliosCharIsAlpha(c) || c == '-' || c == '_' || c == '/' || c == '+' || c == '*' || c == '=' || c == '!' || c == '?';
}

#define ANANAS_LEXER_READ_WHILE(pred) while (1) {                       \
        if (!HeliosString8StreamNext(lexer->contents, &cur_char)) {     \
            break;                                                      \
        }                                                               \
        if (!pred(cur_char)) break;                                     \
        ++lexer->col;                                                   \
    }

B32 AnanasLexerNext(AnanasLexer *lexer, AnanasToken *token) {
    HeliosChar cur_char;

    while (1) {
        if (!HeliosString8StreamNext(lexer->contents, &cur_char)) return 0;
        if (cur_char == ' ' || cur_char == '\t') {
            ++lexer->col;
            continue;
        } else if (cur_char == '\n') {
            ++lexer->row;
            lexer->col = 1;
            continue;
        } else {
            ++lexer->col;
            break;
        }
    }

    switch (cur_char) {
    case '(': {
        token->type = AnanasTokenType_LeftParen;
        token->value.data = lexer->contents->data + lexer->contents->byte_offset;
        token->value.count = 1;
        token->col = lexer->col;
        token->row = lexer->row;
        return 1;
    }
    case ')': {
        token->type = AnanasTokenType_RightParen;
        token->value.data = lexer->contents->data + lexer->contents->byte_offset;
        token->value.count = 1;
        token->col = lexer->col;
        token->row = lexer->row;
        return 1;
    }
    case '"': {
        token->row = lexer->row;
        token->col = lexer->col;

        AnanasTokenType token_type = AnanasTokenType_String;
        UZ string_start = lexer->contents->byte_offset + 1;

        do {
            if (!HeliosString8StreamNext(lexer->contents, &cur_char)) {
                token_type = AnanasTokenType_UnclosedString;
                break;
            }

            if (cur_char == '\n') {
                ++lexer->row;
                lexer->col = 1;
            } else {
                ++lexer->col;
            }
        } while (cur_char != '"');

        UZ string_end = lexer->contents->byte_offset;

        token->type = token_type;
        token->value.data = lexer->contents->data + string_start;
        token->value.count = string_end - string_start;

        return 1;
    }
    default: {
        token->col = lexer->col;
        token->row = lexer->row;
        UZ start = lexer->contents->byte_offset;

        if (AnanasIsSymbolChar(cur_char)) {
            token->type = AnanasTokenType_Symbol;

            ANANAS_LEXER_READ_WHILE(AnanasIsSymbolChar);
        } else if (HeliosCharIsDigit(cur_char)) {
            token->type = AnanasTokenType_Int;

            ANANAS_LEXER_READ_WHILE(HeliosCharIsDigit);
        } else if (AnanasIsReaderMacroChar(cur_char)) {
            token->type = AnanasTokenType_ReaderMacro;

            ANANAS_LEXER_READ_WHILE(AnanasIsReaderMacroChar);
        }

        token->value.data = lexer->contents->data + start;
        token->value.count = lexer->contents->byte_offset - start;

        HeliosString8StreamRetreat(lexer->contents);

        return 1;
    }
    }
}

void AnanasLexerInit(AnanasLexer *lexer, HeliosString8Stream *contents) {
    lexer->contents = contents;
    lexer->col = 0;
    lexer->row = 1;
}
