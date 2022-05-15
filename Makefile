CFLAGS=-Werror -Wall -Wextra -fsanitize=undefined,address -pedantic
LDLIBS=-lm -lbsd

tg2: tg2.c
