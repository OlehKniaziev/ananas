#!/bin/sh

set -xe

clang -o ananas -Wall -Wextra -Werror -O0 -g ./src/main.c ./src/lexer.c ./src/reader.c ./src/astron.c ./src/common.c ./src/interpreter.c
