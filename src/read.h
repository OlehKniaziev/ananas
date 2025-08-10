#ifndef ANANAS_READER_H_
#define ANANAS_READER_H_

#include "value.h"

B32 AnanasReaderNext(AnanasLexer *lexer, AnanasArena *arena, AnanasValue *node, AnanasErrorContext *error_ctx);

#endif // ANANAS_READER_H_
