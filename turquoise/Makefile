CC = gcc
CFLAGS = -ggdb3 -Wall -Werror `pkg-config --cflags glib-2.0 purple` -I../libmotmot/include
LDFLAGS = `pkg-config --libs glib-2.0 purple` -lmsgpack
LIBTOOL = glibtool --tag=CC

DEPDIR = .deps/
INSTALLDIR = /usr/local/lib/purple-2/

SRC = $(wildcard *.c)

all: libmotmot.la tags

libmotmot.la: $(SRC:.c=.o)
	$(LIBTOOL) --mode=link $(CC) $(LDFLAGS) -o libmotmot.la $(SRC:.c=.lo) -rpath $(INSTALLDIR)

.c.o:
	@mkdir -p $(DEPDIR)
	$(LIBTOOL) --mode=compile $(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)$(<:.c=.d) -c $< -o $@

tags: $(wildcard *.c) $(wildcard *.h)
	ctags -R

install:
	$(LIBTOOL) --mode=install cp libmotmot.la $(INSTALLDIR)/libmotmot.la

clean:
	-$(LIBTOOL) --mode=clean rm -rf $(SRC:.c=.lo) libmotmot.la
	-rm -rf $(DEPDIR) tags


-include $(addprefix $(DEPDIR),$(SRC:.c=.d))
