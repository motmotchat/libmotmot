CC = gcc
CFLAGS = -ggdb3 -Wall -Werror `pkg-config --cflags glib-2.0` -O2
LDFLAGS = `pkg-config --libs glib-2.0` -lmsgpack

DEPDIR = .deps/
SRC = $(wildcard *.c) $(wildcard types/*.c)

all: trill tags

trill: $(SRC:.c=.o)
	$(CC) $(LDFLAGS) -o trill $(SRC:.c=.o)

tags: $(wildcard *.c) $(wildcard *.h)
	ctags -R

clean:
	-rm -rf $(DEPDIR) $(SRC:.c=.o) trill


-include $(addprefix $(DEPDIR),$(SRC:.c=.d))
