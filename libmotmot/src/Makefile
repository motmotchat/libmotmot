CC = gcc
CFLAGS = -ggdb3 -Wall -Werror `pkg-config --cflags glib-2.0` -O2

MURMURDIR = ../ext/murmurhash
MURMUR = $(MURMURDIR)/murmurhash3

CFLAGS += -I../include -I$(MURMURDIR)
LDFLAGS = `pkg-config --libs glib-2.0` -lmsgpack

DEPDIR = .deps/
SRC = $(wildcard *.c)

ifdef DEBUG
	CFLAGS += -DDEBUG
endif

all: motmot tags

motmot: $(SRC:.c=.o) murmurhash
	$(CC) $(LDFLAGS) -o motmot $(SRC:.c=.o) $(MURMUR).o

.c.o:
	@mkdir -p $(DEPDIR)
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)$(<:.c=.d) -c $< -o $@

murmurhash:
	$(CC) $(CFLAGS) -c $(MURMUR).c -o $(MURMUR).o

tags: $(wildcard *.c) $(wildcard *.h)
	ctags -R

clean: disconn
	-rm -rf $(DEPDIR) $(SRC:.c=.o) motmot

disconn:
	-rm -rf conn/*

-include $(addprefix $(DEPDIR),$(SRC:.c=.d))
