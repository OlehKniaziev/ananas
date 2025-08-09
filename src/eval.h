#ifndef ANANAS_EVAL_H_
#define ANANAS_EVAL_H_

#include "astron.h"
#include "read.h"

ERMIS_DECL_HASHMAP(HeliosStringView, AnanasASTNode, AnanasEnvMap)

typedef struct AnanasEnv {
    struct AnanasEnv *parent_env;
    AnanasEnvMap map;
} AnanasEnv;

void AnanasEnvInit(AnanasEnv *env, AnanasEnv *parent_env, HeliosAllocator allocator);

B32 AnanasEval(AnanasASTNode node, AnanasArena *arena, AnanasEnv *env, AnanasASTNode *result, AnanasErrorContext *error_ctx);

#endif // ANANAS_EVAL_H_
