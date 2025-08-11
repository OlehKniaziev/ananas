#include "read.h"
#include "eval.h"

static const char *token_type_str_table[] = {
#define X(t) #t,
    ANANAS_ENUM_TOKEN_TYPES
#undef X
};

#define ANANAS_ENUM_READER_MACROS \
    X("`", AnanasGraveMacro) \
    X(",", AnanasUnquoteMacro)

#define X(_name, proc) ANANAS_DECLARE_NATIVE_MACRO(proc);
ANANAS_ENUM_READER_MACROS
#undef X

ANANAS_DECLARE_NATIVE_MACRO(AnanasGraveMacro) {
    ANANAS_CHECK_ARGS_COUNT(1);

    AnanasList *results_list = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
    results_list->car.type = AnanasValueType_Symbol;
    results_list->car.u.symbol = HELIOS_SV_LIT("quote");

    results_list->cdr = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
    results_list->cdr->car = AnanasArgAt(args, 0);

    result->type = AnanasValueType_List;
    result->u.list = results_list;
    return 1;
}

ANANAS_DECLARE_NATIVE_MACRO(AnanasUnquoteMacro) {
    ANANAS_CHECK_ARGS_COUNT(1);

    AnanasList *results_list = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
    results_list->car.type = AnanasValueType_Symbol;
    results_list->car.u.symbol = HELIOS_SV_LIT("unquote");

    results_list->cdr = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);
    results_list->cdr->car = AnanasArgAt(args, 0);

    result->type = AnanasValueType_List;
    result->u.list = results_list;
    return 1;
}

ERMIS_IMPL_HASHMAP(HeliosStringView, AnanasMacro *, AnanasReaderMacroTable, HeliosStringViewEqual, AnanasFnv1Hash)

void AnanasReaderTableInit(AnanasReaderTable *table, HeliosAllocator allocator) {
    AnanasReaderMacroTableInit(&table->reader_macros, allocator, 30);

#define X(name, macro_proc) { \
    AnanasMacro *macro = HeliosAlloc(allocator, sizeof(AnanasMacro)); \
    macro->is_native = 1; \
    macro->u.native = (macro_proc); \
    AnanasReaderMacroTableInsert(&table->reader_macros, HELIOS_SV_LIT((name)), macro); \
    }
ANANAS_ENUM_READER_MACROS
#undef X
}

B32 AnanasReaderNext(AnanasLexer *lexer,
                     AnanasReaderTable *table,
                     AnanasArena *arena,
                     AnanasValue *result,
                     AnanasErrorContext *error_ctx) {
    AnanasToken token;
    if (!AnanasLexerNext(lexer, &token)) return 0;

    switch (token.type) {
    case AnanasTokenType_Int: {
        HELIOS_ASSERT(HeliosParseS64DetectBase(token.value, &result->u.integer));
        result->type = AnanasValueType_Int;
        result->token = token;
        return 1;
    }
    case AnanasTokenType_String: {
        result->type = AnanasValueType_String;
        result->u.string = token.value;
        result->token = token;
        return 1;
    }
    case AnanasTokenType_Symbol: {
        result->type = AnanasValueType_Symbol;
        result->u.symbol = token.value;
        result->token = token;
        return 1;
    }
    case AnanasTokenType_LeftParen: {
        AnanasList *result_list = NULL;
        AnanasList *list = result_list;
        result->token = token;

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
            if (!AnanasReaderNext(lexer, table, arena, &list_car->car, error_ctx)) {
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

        result->type = AnanasValueType_List;
        result->u.list = result_list;

        return 1;
    }
    case AnanasTokenType_ReaderMacro: {
        AnanasMacro *reader_macro = NULL;

        if (!AnanasReaderMacroTableFind(&table->reader_macros, token.value, &reader_macro)) {
            AnanasErrorContextMessage(error_ctx,
                                      token.row,
                                      token.col,
                                      "undefined reader macro '" HELIOS_SV_FMT "'",
                                      HELIOS_SV_ARG(token.value));
            return 0;
        }

        AnanasList *macro_arg = ANANAS_ARENA_STRUCT_ZERO(arena, AnanasList);

        if (!AnanasReaderNext(lexer, table, arena, &macro_arg->car, error_ctx)) return 0;

        return AnanasEvalMacroWithArgumentList(reader_macro,
                                               token,
                                               macro_arg,
                                               arena,
                                               NULL,
                                               error_ctx,
                                               result);
    }
    default: {
        const char *token_type_str = token_type_str_table[token.type];
        AnanasErrorContextMessage(error_ctx, token.row, token.col, "unexpected token '%s'", token_type_str);
        return 0;
    }
    }
}
