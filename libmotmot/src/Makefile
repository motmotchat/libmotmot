CC=gcc
CFLAGS=-ggdb3 `pkg-config --cflags glib-2.0`
LDFLAGS=`pkg-config --libs glib-2.0` -lmsgpack

all: lol paxos.o paxos_helper.o

lol: main.o
	$(CC) $(LDFLAGS) -o motmot *.o
	cp motmot ../

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -rf *.o conn/* lol
