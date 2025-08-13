#ifndef ANANAS_PRINT_H_
#define ANANAS_PRINT_H_

#include "helios.h"
#include "read.h"

HeliosStringView AnanasPrint(HeliosAllocator allocator, AnanasValue value);

static inline void AnanasPrintStdout(AnanasValue value) {
    HeliosAllocator temp = HeliosGetTempAllocator();
    HeliosStringView string = AnanasPrint(temp, value);
    printf(HELIOS_SV_FMT "\n", HELIOS_SV_ARG(string));
}

#endif // ANANAS_PRINT_H_
