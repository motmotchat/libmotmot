import gevent
import gevent.socket
import msgpack

from pprint import pprint as pp

if __name__ == '__main__':
    sock = gevent.socket.create_connection(('127.0.0.1', 8888))
    packer = msgpack.Packer()
    unpacker = msgpack.Unpacker()
    while True:
        obj = input("Type an object: ")
        sock.send(packer.pack(obj))
        unpacker.feed(sock.recv(4096))
        for o in unpacker:
            pp(o)
