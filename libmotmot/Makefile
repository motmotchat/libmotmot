CC=gcc
CFLAGS=-ggdb3 `pkg-config --cflags glib-2.0`
LDFLAGS=`pkg-config --libs glib-2.0`

all: main.o
	$(CC) $(LDFLAGS) -o lol *.o

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -rf *.o conn/* lol
