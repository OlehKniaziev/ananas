#include <stdio.h>

#include "interpreter.h"
#include "common.h"

int main() {
    AnanasArena arena;
    AnanasArenaInit(&arena, 64 * 1024 * 1024);

    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(&arena);

    AnanasEnv env;
    AnanasEnvInit(&env, NULL, arena_allocator);

    while (1) {
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

        AnanasASTNode node;
        if (!AnanasReaderNext(&source, &arena, &node)) {
            printf("Reader error\n");
            continue;
        }

        AnanasASTNode result;
        if (!AnanasEval(node, &arena, &env, &result)) {
            printf("Eval error\n");
            continue;
        }

        HeliosStringView printed_value = AnanasPrint(arena_allocator, result);
        printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(printed_value));
    }
}
