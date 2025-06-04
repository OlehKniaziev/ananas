#include "reader.h"

B32 AnanasReaderNext(HeliosString8Stream *stream, AnanasArena *arena, AnanasASTNode *node) {
    AnanasToken token;
    if (!AnanasLexerNext(stream, &token)) return 0;

    switch (token.type) {
    case AnanasTokenType_Int: {
        HELIOS_ASSERT(HeliosParseS64DetectBase(token.value, &node->u.integer));
        node->type = AnanasASTNodeType_Int;
        return 1;
    }
    case AnanasTokenType_String: {
        node->type = AnanasASTNodeType_String;
        node->u.string = token.value;
        return 1;
    }
    case AnanasTokenType_Symbol: {
        node->type = AnanasASTNodeType_Symbol;
        node->u.symbol = token.value;
        return 1;
    }
    case AnanasTokenType_LeftParen: {
        AnanasList *result_list = NULL;
        AnanasList *list = result_list;

        while (1) {
            HeliosString8Stream prev_stream = *stream;
            if (!AnanasLexerNext(stream, &token)) HELIOS_PANIC("EOF");

            if (token.type == AnanasTokenType_RightParen) {
                break;
            }

            *stream = prev_stream;

            AnanasList *list_car = ANANAS_ARENA_PUSH_ZERO(arena, sizeof(AnanasList));
            if (!AnanasReaderNext(stream, arena, &list_car->car)) HELIOS_PANIC("LIST ELEM EOF");

            if (result_list == NULL) {
                result_list = list_car;
                list = result_list;
            } else {
                list->cdr = list_car;
                list = list->cdr;
            }
        }

        node->type = AnanasASTNodeType_List;
        node->u.list = result_list;

        return 1;
    }
    default: HELIOS_PANIC("Bad token");
    }
}
