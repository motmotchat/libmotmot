CC = gcc
CFLAGS = -ggdb3 -Wall -Werror `pkg-config --cflags glib-2.0`
LDFLAGS = `pkg-config --libs glib-2.0` -lmsgpack

DEPDIR = .deps/
SRC = $(wildcard *.c)

ifdef DEBUG
	CFLAGS += -DDEBUG
endif

all: motmot

motmot: $(SRC:.c=.o)
	$(CC) $(LDFLAGS) -o motmot $(SRC:.c=.o)

.c.o:
	@mkdir -p $(DEPDIR)
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)$(<:.c=.d) -c $< -o $@

tags:
	ctags -R

clean: disconn
	-rm -rf $(DEPDIR) $(SRC:.c=.o) motmot

disconn:
	-rm -rf conn/*

-include $(addprefix $(DEPDIR),$(SRC:.c=.d))
