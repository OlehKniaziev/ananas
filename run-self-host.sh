#!/usr/bin/bash

set -xe

./ananas ./self-host/compiler.ans > out.asm
as -o out.o out.asm
cc -fPIE -o out out.o -lc
./out
