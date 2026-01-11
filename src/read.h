#ifndef ANANAS_READER_H_
#define ANANAS_READER_H_

#include "value.h"

ERMIS_DECL_HASHMAP(HeliosStringView, AnanasMacro *, AnanasReaderMacroTable)

typedef struct {
    AnanasReaderMacroTable reader_macros;
} AnanasReaderTable;

void AnanasReaderTableInit(AnanasReaderTable *, HeliosAllocator);

B32 AnanasReaderNext(AnanasLexer *lexer,
                     AnanasReaderTable *table,
                     HeliosAllocator allocator,
                     AnanasValue *value,
                     AnanasErrorContext *error_ctx);

#endif // ANANAS_READER_H_
