#ifndef ANANAS_CODEGEN_H_
#define ANANAS_CODEGEN_H_

#include "astron.h"

typedef struct {
    HeliosStringView source;
} AnanasCG_Program;

B32 AnanasCG_CompileSource(HeliosAllocator, HeliosStringView, AnanasCG_Program *);

#endif // ANANAS_CODEGEN_H_
