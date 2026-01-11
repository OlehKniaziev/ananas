#include <stdio.h>

#include "eval.h"
#include "print.h"
#include "common.h"
#include "son.h"
#include "lir.h"
#include "vm.h"
#include "platform.h"

static void AnanasEvalFile(HeliosAllocator allocator, HeliosStringView file_path) {
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
    while (AnanasReaderNext(&lexer, &reader_table, allocator, &node, &error_ctx)) {
        AnanasValue result;
        if (!AnanasEval(node, allocator, &env, &result, &error_ctx)) {
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
    while (AnanasReaderNext(&lexer, &reader_table, allocator, &value, &error_ctx)) {
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

static void AnanasLIR_DumpBytecode(U8 *bcode, UZ bcount) {
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
        printf(HELIOS_UZ_FMT "] %s ", i, op_name);

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
        case AnanasLIR_Op_Update: {
            AnanasLIR_OpUpdate *iop = (AnanasLIR_OpUpdate *)op;
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
        case AnanasLIR_Op_Call: {
            AnanasLIR_OpCall *cop = (AnanasLIR_OpCall *)op;
            printf("%u", cop->args_count);
            i += sizeof(*cop);
            break;
        }
        case AnanasLIR_Op_PushScope:
        case AnanasLIR_Op_PopScope:
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

static void AnanasLIR_CompileFile(AnanasArena *arena,
                                  HeliosStringView file_path,
                                  AnanasLIR_CompiledModule *module) {
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
    while (AnanasReaderNext(&lexer, &reader_table, allocator, &value, &error_ctx)) {
        AnanasValueArrayPush(&program, value);
    }

    if (!error_ctx.ok) {
        fprintf(stderr, "Reader error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
        exit(1);
    }

    AnanasLIR_CompilerContext ctx = {0};
    AnanasLIR_CompilerContextInit(&ctx, allocator, arena);

    B32 ok = AnanasLIR_CompileProgram(&ctx, program, module);
    HELIOS_ASSERT(ok);
}

int main(int argc, char **argv) {
    AnanasArena arena;
    AnanasArenaInit(&arena, 64 * 1024 * 1024);

    if (argc == 2) {
        fprintf(stderr, "not enough arguments");
        return 1;
    }

    HeliosAllocator arena_allocator = AnanasArenaToHeliosAllocator(&arena);

    if (argc >= 3) {
        const char *subcommand = argv[1];
        if (strcmp(subcommand, "run") == 0) {
            HeliosStringView file_path = HELIOS_SV_LIT(argv[2]);
            AnanasEvalFile(arena_allocator, file_path);
        } else if (strcmp(subcommand, "com") == 0) {
            HeliosStringView file_path = HELIOS_SV_LIT(argv[2]);
            AnanasLIR_CompiledModule module = {0};
            AnanasLIR_CompileFile(&arena, file_path, &module);

            AnanasLIR_DumpBytecode(module.bytecode, module.bytecode_count);

            for (UZ i = 0; i < module.lambdas_count; ++i) {
                AnanasLIR_CompiledLambda lam = module.lambdas[i];
                printf("Lambda %zu:\n", i);
                AnanasLIR_DumpBytecode(lam.bytecode, lam.bytecode_count);
            }

            exit(0);
        } else if (strcmp(subcommand, "brun") == 0) {
            HeliosStringView file_path = HELIOS_SV_LIT(argv[2]);
            AnanasLIR_CompiledModule module = {0};
            AnanasLIR_CompileFile(&arena, file_path, &module);

            AnanasLIR_DumpBytecode(module.bytecode, module.bytecode_count);

            for (UZ i = 0; i < module.lambdas_count; ++i) {
                AnanasLIR_CompiledLambda lam = module.lambdas[i];
                printf("Lambda %zu:\n", i);
                AnanasLIR_DumpBytecode(lam.bytecode, lam.bytecode_count);
            }

            HeliosAllocator allocator = HeliosNewMallocAllocator();

            AnanasVM vm = {0};
            AnanasVM_Init(&vm, allocator);

            HELIOS_VERIFY(AnanasVM_ExecModule(&vm, module));

            exit(0);
        } else {
            fprintf(stderr, "unknown subcommand %s", subcommand);
            return 1;
        }

        HELIOS_UNREACHABLE();
    }

    HeliosAllocator malloc_allocator = HeliosNewMallocAllocator();

    AnanasEnv env;
    AnanasEnvInit(&env, NULL, arena_allocator);
    AnanasRootEnvPopulate(&env);

    AnanasReaderTable reader_table;
    AnanasReaderTableInit(&reader_table, arena_allocator);

    while (1) {
        U8 error_buffer[1024] = {0};
        AnanasErrorContext error_ctx = {0};
        AnanasErrorContextInit(&error_ctx, error_buffer, sizeof(error_buffer));

        printf("> ");

        U8 *line_buffer = NULL;
        UZ line_count = 0;

        if (!AnanasPlatformGetLine(malloc_allocator, &line_buffer, &line_count)) {
            printf("\n");
            break;
        }

        HeliosStringView line = {.data = line_buffer, .count = line_count};

        HeliosString8Stream source;
        HeliosString8StreamInit(&source, (U8 *)line.data, line.count);

        AnanasLexer lexer;
        AnanasLexerInit(&lexer, &source);

        AnanasValue node;
        if (!AnanasReaderNext(&lexer, &reader_table, arena_allocator, &node, &error_ctx)) {
            if (!error_ctx.ok) {
                printf("Reader error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
            }

            continue;
        }

        AnanasValue result;
        if (!AnanasEval(node, arena_allocator, &env, &result, &error_ctx)) {
            printf("Eval error: " HELIOS_SV_FMT "\n", HELIOS_SV_ARG(error_ctx.error_buffer));
            continue;
        }

        HeliosStringView printed_value = AnanasPrint(arena_allocator, result);
        printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(printed_value));
    }
}
