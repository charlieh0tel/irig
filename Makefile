CFLAGS := -Werror -Wall -Wextra -pedantic
LDLIBS := -lbsd -lm

#CFLAGS += -fsanitize=undefined,address

# Argh.  Raspbian 11 is missing pkg-config. 
#CFLAGS += $(shell pkg-config --cflags portaudio-2.0)
#LDLIBS += $(shell pkg-config --libs portaudio-2.0)
#
CFLAGS += -pthread
LDLIBS += -lportaudio -lasound -lm -lpthread


tg2: tg2.c

style:
	clang-format --style="{BasedOnStyle: Google, ColumnLimit: 120}" -i tg2.c
.PHONY: style
