#include "lexer.h"

#include "common.h"

static B32 AnanasIsReaderMacroChar(HeliosChar c) {
    return c == '`' ||
           c == '@' ||
           c == ',' ||
           c == '#' ||
           c == '~' ||
           c == '\'';
}

static B32 AnanasIsFirstSymbolChar(HeliosChar c) {
    return HeliosCharIsAlpha(c)
           || c == '-'
           || c == '_'
           || c == '/'
           || c == '+'
           || c == '*'
           || c == '='
           || c == '!'
           || c == '?'
           || c == '.';
}

static B32 AnanasIsSymbolChar(HeliosChar c) {
    return AnanasIsFirstSymbolChar(c) || HeliosCharIsDigit(c);
}

#define ANANAS_LEXER_READ_WHILE(pred) while (1) {                       \
        if (!HeliosString8StreamNext(lexer->contents, &cur_char)) {     \
            break;                                                      \
        }                                                               \
        if (!pred(cur_char)) break;                                     \
        ++lexer->col;                                                   \
    }

B32 AnanasLexerNext(AnanasLexer *lexer, HeliosAllocator allocator, AnanasToken *token) {
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
        AnanasDString string_value;
        AnanasDStringInit(&string_value, allocator, 7);

        while (1) {
            if (!HeliosString8StreamNext(lexer->contents, &cur_char)) {
                token_type = AnanasTokenType_UnclosedString;
                break;
            }

            if (cur_char == '\\') {
                if (!HeliosString8StreamNext(lexer->contents, &cur_char)) {
                    token_type = AnanasTokenType_UnclosedString;
                    break;
                }

                switch (cur_char) {
                case 'n': {
                    AnanasDStringPush(&string_value, '\n');
                    break;
                }
                case 'r': {
                    AnanasDStringPush(&string_value, '\r');
                    break;
                }
                case '\\': {
                    AnanasDStringPush(&string_value, '\\');
                    break;
                }
                case '"': {
                    AnanasDStringPush(&string_value, '"');
                    break;
                }
                default: {
                    token_type = AnanasTokenType_UnclosedString;
                    goto loop_end;
                }
                }
            } else if (cur_char == '"') {
                break;
            } else {
                AnanasDStringPush(&string_value, cur_char);

                if (cur_char == '\n') {
                    ++lexer->row;
                    lexer->col = 1;
                } else {
                    ++lexer->col;
                }
            }
        }

    loop_end:

        token->type = token_type;
        token->value.data = string_value.items;
        token->value.count = string_value.count;

        return 1;
    }
    default: {
        token->col = lexer->col;
        token->row = lexer->row;
        UZ start = lexer->contents->byte_offset;

        if (AnanasIsFirstSymbolChar(cur_char)) {
            token->type = AnanasTokenType_Symbol;

            ANANAS_LEXER_READ_WHILE(AnanasIsSymbolChar);
        } else if (HeliosCharIsDigit(cur_char)) {
            token->type = AnanasTokenType_Int;

            ANANAS_LEXER_READ_WHILE(HeliosCharIsDigit);
        } else if (AnanasIsReaderMacroChar(cur_char)) {
            token->type = AnanasTokenType_ReaderMacro;

            ANANAS_LEXER_READ_WHILE(AnanasIsReaderMacroChar);
        } else {
            token->type = AnanasTokenType_Illegal;
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
