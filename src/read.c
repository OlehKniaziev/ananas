#include "read.h"

static const char *token_type_str_table[] = {
};

B32 AnanasReaderNext(AnanasLexer *lexer, AnanasArena *arena, AnanasASTNode *node, AnanasErrorContext *error_ctx) {
    AnanasToken token;
    if (!AnanasLexerNext(lexer, &token)) return 0;

    switch (token.type) {
    case AnanasTokenType_Int: {
        HELIOS_ASSERT(HeliosParseS64DetectBase(token.value, &node->u.integer));
        node->type = AnanasASTNodeType_Int;
        node->token = token;
        return 1;
    }
    case AnanasTokenType_String: {
        node->type = AnanasASTNodeType_String;
        node->u.string = token.value;
        node->token = token;
        return 1;
    }
    case AnanasTokenType_Symbol: {
        node->type = AnanasASTNodeType_Symbol;
        node->u.symbol = token.value;
        node->token = token;
        return 1;
    }
    case AnanasTokenType_LeftParen: {
        AnanasList *result_list = NULL;
        AnanasList *list = result_list;
        node->token = token;

        while (1) {
            AnanasLexer prev_lexer = *lexer;
            HeliosString8Stream prev_contents = *lexer->contents;
            if (!AnanasLexerNext(lexer, &token)) {
                AnanasErrorContextMessage(error_ctx, token.row, token.col + token.value.count, "EOF while expecting ')'");
                return 0;
            }

            if (token.type == AnanasTokenType_RightParen) {
                break;
            }

            *lexer = prev_lexer;
            *lexer->contents = prev_contents;

            AnanasList *list_car = ANANAS_ARENA_PUSH_ZERO(arena, sizeof(AnanasList));
            if (!AnanasReaderNext(lexer, arena, &list_car->car, error_ctx)) {
                HELIOS_ASSERT(!error_ctx->ok);
                return 0;
            }

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
    default: {
        const char *token_type_str = token_type_str_table[token.type];
        AnanasErrorContextMessage(error_ctx, token.row, token.col, "unexpected token '%s'", token_type_str);
        return 0;
    }
    }
}
