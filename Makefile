CFLAGS := -Werror -Wall -Wextra -fsanitize=undefined,address -pedantic
LDFLAGS := -lbsd -lm

CFLAGS += $(shell pkg-config --cflags portaudio-2.0)
LDFLAGS += $(shell pkg-config --libs portaudio-2.0)

tg2: tg2.c

style:
	clang-format --style="{BasedOnStyle: Google, ColumnLimit: 120}" -i tg2.c
.PHONY: style
