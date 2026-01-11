#ifndef ANANAS_EVAL_H_
#define ANANAS_EVAL_H_

#include "astron.h"
#include "read.h"

ERMIS_DECL_HASHMAP(HeliosStringView, AnanasValue, AnanasEnvMap)

typedef struct AnanasEnv {
    struct AnanasEnv *parent_env;
    AnanasEnvMap map;
} AnanasEnv;

void AnanasEnvInit(AnanasEnv *env, AnanasEnv *parent_env, HeliosAllocator allocator);
void AnanasRootEnvPopulate(AnanasEnv *env);

B32 AnanasEvalMacroWithArgumentList(AnanasMacro *macro,
                                    AnanasToken where,
                                    AnanasList *args_list,
                                    HeliosAllocator allocator,
                                    AnanasErrorContext *error_ctx,
                                    AnanasValue *result);

B32 AnanasEval(AnanasValue node, HeliosAllocator allocator, AnanasEnv *env, AnanasValue *result, AnanasErrorContext *error_ctx);

#endif // ANANAS_EVAL_H_
