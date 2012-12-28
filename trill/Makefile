CC = gcc
CFLAGS = -ggdb3 -Wall -Werror `pkg-config --cflags glib-2.0 gio-2.0 gnutls` -O2 -DTRILL_USE_GNUTLS=1
LDFLAGS = `pkg-config --libs glib-2.0 gio-2.0 gnutls` -lmsgpack

DEPDIR = .deps/
SRC = $(filter-out plume.c, $(wildcard *.c)) $(wildcard types/*.c)

all: trill plume tags

trill: $(SRC:.c=.o)
	$(CC) $(LDFLAGS) -o trill $(SRC:.c=.o)

plume: plume.o log.o
	$(CC) $(LDFLAGS) -o plume plume.o log.o

.c.o:
	@mkdir -p $(DEPDIR)
	@mkdir -p $(DEPDIR)/types
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)$(<:.c=.d) -c $< -o $@

tags: $(wildcard *.c) $(wildcard *.h)
	ctags -R

clean:
	-rm -rf $(DEPDIR) $(SRC:.c=.o) trill


-include $(addprefix $(DEPDIR),$(SRC:.c=.d))
