#include <stdio.h>

#include "eval.h"
#include "common.h"

int main() {
    AnanasArena arena;
    AnanasArenaInit(&arena, 64 * 1024 * 1024);

    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(&arena);

    AnanasEnv env;
    AnanasEnvInit(&env, NULL, arena_allocator);

    while (1) {
        U8 backing_error_buffer[1024];
        HeliosStringView error_buffer = {.data = backing_error_buffer, .count = sizeof(backing_error_buffer)};
        AnanasErrorContext error_ctx = {.ok = 1, .place = HELIOS_SV_LIT("repl"), .error_buffer = error_buffer};

        printf("> ");

        char *line = NULL;
        UZ line_count = 0;
        ssize_t res = getline(&line, &line_count, stdin);

        if (res == -1) {
            printf("\n");
            break;
        }

        HeliosString8Stream source;
        HeliosString8StreamInit(&source, (U8 *)line, res);

        AnanasLexer lexer;
        AnanasLexerInit(&lexer, &source);

        AnanasASTNode node;
        if (!AnanasReaderNext(&lexer, &arena, &node, &error_ctx)) {
            printf("Reader error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
            continue;
        }

        AnanasASTNode result;
        if (!AnanasEval(node, &arena, &env, &result, &error_ctx)) {
            printf("Eval error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
            continue;
        }

        HeliosStringView printed_value = AnanasPrint(arena_allocator, result);
        printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(printed_value));
    }
}
