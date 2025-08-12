#include <stdio.h>

#include "eval.h"
#include "print.h"
#include "common.h"

static void AnanasEvalFile(AnanasArena *arena, HeliosStringView file_path) {
    HeliosAllocator allocator = AnanasArenaToHeliosAllocator(arena);

    HeliosStringView file_contents = HeliosReadEntireFile(allocator, file_path);
    if (file_contents.data == NULL) {
        fprintf(stderr, "Failed to read the file\n");
        exit(1);
    }

    HeliosString8Stream source;
    HeliosString8StreamInit(&source, file_contents.data, file_contents.count);

    AnanasLexer lexer;
    AnanasLexerInit(&lexer, &source);

    U8 backing_error_buffer[1024];
    HeliosStringView error_buffer = {.data = backing_error_buffer, .count = sizeof(backing_error_buffer)};
    AnanasErrorContext error_ctx = {.ok = 1, .place = HELIOS_SV_LIT("repl"), .error_buffer = error_buffer};

    AnanasReaderTable reader_table;
    AnanasReaderTableInit(&reader_table, allocator);

    AnanasEnv env;
    AnanasEnvInit(&env, NULL, allocator);
    AnanasRootEnvPopulate(&env);

    AnanasValue node;
    while (AnanasReaderNext(&lexer, &reader_table, arena, &node, &error_ctx)) {
        AnanasValue result;
        if (!AnanasEval(node, arena, &env, &result, &error_ctx)) {
            fprintf(stderr, "Eval error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
            exit(1);
        }
    }

    if (!error_ctx.ok) {
        fprintf(stderr, "Reader error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
        exit(1);
    } else {
        exit(0);
    }
}

int main(int argc, char **argv) {
    AnanasArena arena;
    AnanasArenaInit(&arena, 64 * 1024 * 1024);

    if (argc >= 2) {
        HeliosStringView file_path = HELIOS_SV_LIT(argv[1]);
        AnanasEvalFile(&arena, file_path);
        HELIOS_UNREACHABLE();
    }

    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(&arena);

    AnanasEnv env;
    AnanasEnvInit(&env, NULL, arena_allocator);
    AnanasRootEnvPopulate(&env);

    AnanasReaderTable reader_table;
    AnanasReaderTableInit(&reader_table, arena_allocator);

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

        AnanasValue node;
        if (!AnanasReaderNext(&lexer, &reader_table, &arena, &node, &error_ctx)) {
            if (!error_ctx.ok) {
                printf("Reader error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
            }

            continue;
        }

        AnanasValue result;
        if (!AnanasEval(node, &arena, &env, &result, &error_ctx)) {
            printf("Eval error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
            continue;
        }

        HeliosStringView printed_value = AnanasPrint(arena_allocator, result);
        printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(printed_value));
    }
}
