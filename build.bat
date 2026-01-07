SET commonflags=-Wall -Wextra -Werror -g
SET sources=./src/main.c ./src/lexer.c ./src/read.c ./src/astron.c ./src/common.c ./src/eval.c ./src/print.c ./src/son.c ./src/lir.c ./src/value.c ./src/vm.c ./src/gc.c ./src/platform_win32.c

IF "%1" == release (
    clang -o ananas.exe %commonflags% -O2 %sources%
) ELSE IF "%1" == san (
    clang -o ananas.exe %commonflags% -fsanitize=address -DANANAS_REPLACE_ARENA_WITH_MALLOC -O0 %sources%
) ELSE (
    clang -o ananas.exe %commonflags% -O0 %sources%
)
