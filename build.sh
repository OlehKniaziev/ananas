#!/bin/sh

set -xe

commonflags="-Wall -Wextra -Werror -g"
sources="./src/main.c ./src/lexer.c ./src/read.c ./src/astron.c ./src/common.c ./src/eval.c ./src/print.c"

if [ "$1" = "release" ]; then
    clang -o ananas $commonflags -O2 $sources
elif [ "$1" = "san" ]; then
    clang -o ananas $commonflags -fsanitize=address -DANANAS_REPLACE_ARENA_WITH_MALLOC -O0 $sources
else
    clang -o ananas $commonflags -O0 $sources
fi
