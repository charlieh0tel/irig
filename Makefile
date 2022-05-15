CFLAGS=-Werror -Wall -Wextra -fsanitize=undefined,address -pedantic
LDLIBS=-lm -lbsd

tg2: tg2.c

style:
	clang-format --style=Google -i tg2.c
.PHONY: style
