DEPDIR = .deps/
INSTALLDIR = /home/jeff/262proj/build/lib/purple-2/
LIBMOTMOTDIR=../motmot/

CC = gcc
CFLAGS = -ggdb3 -Wall -Werror `pkg-config --cflags glib-2.0 purple` -I$(LIBMOTMOTDIR)
LDFLAGS = `pkg-config --libs glib-2.0 purple msgpack` -rpath $(INSTALLDIR) -I$(LIBMOTMOTDIR)

LIBTOOL = libtool

SRC = $(wildcard *.c)

all: libmotmot.la

libmotmot.la: $(SRC:.c=.o)
	$(LIBTOOL) --mode=link $(CC) $(LDFLAGS) -o libmotmot.la $(SRC:.c=.lo) 

.c.o:
	@mkdir -p $(DEPDIR)
	$(LIBTOOL) --mode=compile $(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)$(<:.c=.d) -c $< -o $@

tags:
	ctags -R

install:
	$(LIBTOOL) --mode=install cp libmotmot.la $(INSTALLDIR)/libmotmot.la

clean:
	-$(LIBTOOL) --mode=clean rm -rf $(SRC:.c=.lo) libmotmot.la
	-rm -rf $(DEPDIR)


-include $(addprefix $(DEPDIR),$(SRC:.c=.d))
