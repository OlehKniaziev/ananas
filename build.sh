#!/bin/sh

set -xe

clang -o ananas -Wall -Wextra -Werror -O0 -g ./src/main.c ./src/lexer.c ./src/read.c ./src/astron.c ./src/common.c ./src/eval.c ./src/print.c
