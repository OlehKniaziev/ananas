#ifndef ANANAS_PLATFORM_H_
#define ANANAS_PLATFORM_H_

#include "helios.h"

B32 AnanasPlatformGetLine(HeliosAllocator, U8 **, UZ *);

void *AnanasPlatformAllocPages(UZ);

#endif // ANANAS_PLATFORM_H_
