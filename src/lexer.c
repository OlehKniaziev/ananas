#include "lexer.h"

B32 AnanasIsReaderMacroChar(HeliosChar c) {
    return c == '@' ||
           c == ',' ||
           c == '.' ||
           c == '#' ||
           c == '~' ||
           c == '\'';
}

B32 AnanasLexerNext(HeliosString8Stream *contents, AnanasToken *token) {
    HeliosChar cur_char;
    do {
        if (!HeliosString8StreamNext(contents, &cur_char)) return 0;
    } while (cur_char == ' ' || cur_char == '\t' || cur_char == '\n');

    switch (cur_char) {
    case '(': {
        token->type = AnanasTokenType_LeftParen;
        token->value.data = contents->data + contents->byte_offset - 1;
        token->value.count = 1;
        return 1;
    }
    case ')': {
        token->type = AnanasTokenType_RightParen;
        token->value.data = contents->data + contents->byte_offset - 1;
        token->value.count = 1;
        return 1;
    }
    case '"': {
        AnanasTokenType token_type = AnanasTokenType_String;
        UZ string_start = contents->byte_offset;

        do {
            if (!HeliosString8StreamNext(contents, &cur_char)) {
                token_type = AnanasTokenType_UnclosedString;
                break;
            }
        } while (cur_char != '"');

        UZ string_end = contents->byte_offset - 1 - contents->last_char_size;

        token->type = token_type;
        token->value.data = contents->data + string_start;
        token->value.count = string_end - string_start;

        HeliosString8StreamRetreat(contents);

        return 1;
    }
    default: {
        UZ start = contents->byte_offset;

        if (HeliosCharIsAlpha(cur_char)) {
            token->type = AnanasTokenType_Symbol;

            do {
                if (!HeliosString8StreamNext(contents, &cur_char)) {
                    break;
                }
            } while (HeliosCharIsAlpha(cur_char));
        } else if (HeliosCharIsDigit(cur_char)) {
            token->type = AnanasTokenType_Int;

            do {
                if (!HeliosString8StreamNext(contents, &cur_char)) {
                    break;
                }
            } while (HeliosCharIsDigit(cur_char));
        } else if (AnanasIsReaderMacroChar(cur_char)) {
            token->type = AnanasTokenType_ReaderMacro;

            do {
                if (!HeliosString8StreamNext(contents, &cur_char)) {
                    break;
                }
            } while (HeliosCharIsDigit(cur_char));
        }

        token->value.data = contents->data + start;
        token->value.count = contents->byte_offset - start;

        HeliosString8StreamRetreat(contents);

        return 1;
    }
    }
}
