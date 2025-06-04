#ifndef ANANAS_INTERPRETER_H_
#define ANANAS_INTERPRETER_H_

#include "astron.h"
#include "reader.h"

ERMIS_DECL_HASHMAP(HeliosStringView, AnanasASTNode, AnanasEnvMap)

typedef struct AnanasEnv {
    struct AnanasEnv *parent_env;
    AnanasEnvMap map;
} AnanasEnv;

void AnanasEnvInit(AnanasEnv *env, AnanasEnv *parent_env, HeliosAllocator allocator);

B32 AnanasEval(AnanasASTNode node, AnanasArena *arena, AnanasEnv *env, AnanasASTNode *result);
HeliosStringView AnanasPrint(HeliosAllocator allocator, AnanasASTNode value);

#endif // ANANAS_INTERPRETER_H_
