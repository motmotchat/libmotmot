#!/usr/bin/env python

from gevent import socket
import msgpack

if __name__ == '__main__':
    address = ('localhost', 8888)

    sock = socket.socket()
    sock.connect(address)

    test = [1,1,"ebensing","12345"]

    sVal = msgpack.packb(test)

    

    sock.sendall(sVal)

    rVal = sock.recv(4096)
    print msgpack.unpackb(rVal)

    test = [1,2,"julie@bensing.com"]

    sock.sendall(msgpack.packb(test))

    rVal = sock.recv(4096)
    print msgpack.unpackb(rVal)
    

    test = [1,3,"julie@bensing.com"]

    sock.sendall(msgpack.packb(test))

    rVal = sock.recv(4096)
    print rVal
