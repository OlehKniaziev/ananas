#include <stdio.h>

#include "eval.h"
#include "print.h"
#include "common.h"
#include "son.h"
#include "lir.h"

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
    AnanasErrorContext error_ctx = {.ok = 1, .place = file_path, .error_buffer = error_buffer};

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

void AnanasSON_CompileFile(AnanasArena *arena, HeliosStringView file_path) {
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
    AnanasErrorContext error_ctx = {.ok = 1, .place = file_path, .error_buffer = error_buffer};

    AnanasReaderTable reader_table;
    AnanasReaderTableInit(&reader_table, allocator);

    AnanasSON_CompilerState cstate = {0};
    AnanasSON_CompilerStateInit(&cstate, arena);

    AnanasValue value;
    while (AnanasReaderNext(&lexer, &reader_table, arena, &value, &error_ctx)) {
        AnanasSON_Node *node = AnanasSON_Compile(&cstate, value);
        HELIOS_VERIFY(node != NULL);
    }

    if (!error_ctx.ok) {
        fprintf(stderr, "Reader error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
        exit(1);
    }

    HeliosString8 g = {.allocator = allocator};

    AnanasSON_FormatNodeGraphInto(&cstate, &g);
    printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(g));

    exit(0);
}

static void AnanasLIR_DumpBytecode(AnanasLIR_Bytecode bytecode) {
    U8 *bcode = bytecode.bytes;
    UZ bcount = bytecode.count;

    HELIOS_ASSERT(bcode != NULL);

    UZ b = bcount;
    UZ w = 0;
    while (b /= 10) {
        ++w;
    }

    for (UZ i = 0; i < bcount;) {
        AnanasLIR_Op *op = (AnanasLIR_Op *)(bcode + i);
        const char *op_name = AnanasLIR_OpName(*op);

        UZ p = i;
        UZ pw = 0;
        while (p /= 10) {
            ++pw;
        }

        printf("\t[");
        for (; pw < w; ++pw) {
            printf(" ");
        }
        printf("%lu] %s ", i, op_name);

        switch (*op) {
        case AnanasLIR_Op_Const: {
            AnanasLIR_OpConst *cop = (AnanasLIR_OpConst *)op;
            HeliosStringView val = AnanasPrint(HeliosGetTempAllocator(), cop->value);
            printf(HELIOS_SV_FMT, HELIOS_SV_ARG(val));
            i += sizeof(*cop);
            break;
        }
        case AnanasLIR_Op_Lookup: {
            AnanasLIR_OpLookup *lop = (AnanasLIR_OpLookup *)op;
            printf(HELIOS_SV_FMT, HELIOS_SV_ARG(lop->name));
            i += sizeof(*lop);
            break;
        }
        case AnanasLIR_Op_Insert: {
            AnanasLIR_OpInsert *iop = (AnanasLIR_OpInsert *)op;
            printf(HELIOS_SV_FMT, HELIOS_SV_ARG(iop->name));
            i += sizeof(*iop);
            break;
        }
        case AnanasLIR_Op_Define: {
            AnanasLIR_OpDefine *dop = (AnanasLIR_OpDefine *)op;
            printf(HELIOS_SV_FMT, HELIOS_SV_ARG(dop->name));
            i += sizeof(*dop);
            break;
        }
        case AnanasLIR_Op_Jmp: {
            AnanasLIR_OpJmp *jop = (AnanasLIR_OpJmp *)op;
            printf("@%zu", jop->ip);
            i += sizeof(*jop);
            break;
        }
        case AnanasLIR_Op_CondJmp: {
            AnanasLIR_OpCondJmp *jop = (AnanasLIR_OpCondJmp *)op;
            printf("@%zu", jop->ip);
            i += sizeof(*jop);
            break;
        }
        case AnanasLIR_Op_LoadLambda: {
            AnanasLIR_OpLoadLambda *lop = (AnanasLIR_OpLoadLambda *)op;
            printf("[%u]", lop->index);
            i += sizeof(*lop);
            break;
        }
        case AnanasLIR_Op_PushScope:
        case AnanasLIR_Op_PopScope:
        case AnanasLIR_Op_Call:
        case AnanasLIR_Op_Return:
        case AnanasLIR_Op_Rem:
        case AnanasLIR_Op_Sub:
        case AnanasLIR_Op_Mul:
        case AnanasLIR_Op_Add: {
            i += sizeof(*op);
            break;
        }
        }

        printf("\n");
    }
}

static void AnanasLIR_CompileFile(AnanasArena *arena, HeliosStringView file_path) {
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
    AnanasErrorContext error_ctx = {.ok = 1, .place = file_path, .error_buffer = error_buffer};

    AnanasReaderTable reader_table;
    AnanasReaderTableInit(&reader_table, allocator);

    AnanasValueArray program;
    AnanasValueArrayInit(&program, allocator, 333);
    AnanasValue value;
    while (AnanasReaderNext(&lexer, &reader_table, arena, &value, &error_ctx)) {
        AnanasValueArrayPush(&program, value);
    }

    if (!error_ctx.ok) {
        fprintf(stderr, "Reader error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
        exit(1);
    }

    AnanasLIR_CompilerContext ctx = {0};
    AnanasLIR_CompilerContextInit(&ctx, allocator, arena);

    B32 ok = AnanasLIR_CompileProgram(&ctx, program);
    HELIOS_ASSERT(ok);

    AnanasLIR_DumpBytecode(ctx.bytecode);

    for (UZ i = 0; i < ctx.lambdas_count; ++i) {
        AnanasLIR_CompiledLambda lam = ctx.lambdas[i];
        printf("Lambda %zu:\n", i);
        AnanasLIR_DumpBytecode(lam.bytecode);
    }

    exit(0);
}

int main(int argc, char **argv) {
    AnanasArena arena;
    AnanasArenaInit(&arena, 64 * 1024 * 1024);

    if (argc == 2) {
        fprintf(stderr, "not enough arguments");
        return 1;
    }

    if (argc >= 3) {
        const char *subcommand = argv[1];
        if (strcmp(subcommand, "run") == 0) {
            HeliosStringView file_path = HELIOS_SV_LIT(argv[2]);
            AnanasEvalFile(&arena, file_path);
        } else if (strcmp(subcommand, "com") == 0) {
            HeliosStringView file_path = HELIOS_SV_LIT(argv[2]);
            AnanasLIR_CompileFile(&arena, file_path);
        } else {
            fprintf(stderr, "unknown subcommand %s", subcommand);
            return 1;
        }

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
