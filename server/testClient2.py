#!/usr/bin/env python

from gevent import socket
import msgpack
import time
import socket as bSock

if __name__ == '__main__':
    address = (bSock.gethostbyname('other.com'), 8888)

    sock = socket.socket()
    sock.connect(address)

    test = [1,1,"test@other.com","12345"]

    sVal = msgpack.packb(test)



    sock.sendall(sVal)

    rVal = sock.recv(4096)
    print msgpack.unpackb(rVal)

    test = [1,3,"ej@bensing.com"]

    sock.sendall(msgpack.packb(test))

    rVal = sock.recv(4096)
    print msgpack.unpackb(rVal)
    """
    test = [1,3,"ej@bensing.com"]

    sock.sendall(msgpack.packb(test))

    rVal = msgpack.unpackb(sock.recv(4096))
    print rVal
    """
    sock.close()
    """
    sock2 = socket.socket()
    sock2.connect(address)


    test = [1,1,"test@bensing.com", "12345"]

    sock2.sendall(msgpack.packb(test))

    rVal = msgpack.unpackb(sock2.recv(4096))
    print rVal


    test = [1,6,"ej@bensing.com"]

    sock2.sendall(msgpack.packb(test))

    rVal = msgpack.unpackb(sock2.recv(4096))
    print rVal
    """

