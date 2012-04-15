CC=gcc
CFLAGS=-ggdb3 -Wall -Werror `pkg-config --cflags glib-2.0`
LDFLAGS=`pkg-config --libs glib-2.0` -lmsgpack

all: tags motmot paxos.o paxos_helper.o paxos_msgpack.o

motmot: main.o
	$(CC) $(LDFLAGS) -o motmot *.o

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

tags:
	ctags -R

clean:
	-rm -rf *.o conn/* motmot
