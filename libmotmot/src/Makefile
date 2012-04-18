CC=gcc
CFLAGS=-ggdb3 -Wall -Werror `pkg-config --cflags glib-2.0`
LDFLAGS=`pkg-config --libs glib-2.0` -lmsgpack

all: tags motmot motmot.o paxos.o paxos_helper.o paxos_msgpack.o

motmot: main.o
	$(CC) $(LDFLAGS) -o motmot obj/*.o

.c.o:
	$(CC) $(CFLAGS) -c $< -o obj/$@

tags:
	ctags -R

clean:
	-rm -rf obj/*.o conn/* motmot
