import sys
import socket
import msgpack

from pprint import pprint

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 8888))

while True:
    print '>',
    obj = input()
    sock.send(msgpack.packb(obj))
    pprint(msgpack.unpackb(sock.recv(1024)))
