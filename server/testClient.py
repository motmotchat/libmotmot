#!/usr/bin/env python

from gevent import socket
from gevent import ssl
from gevent import Greenlet
from gevent import queue
import msgpack
import time
import socket as bSock

import motmot
from pprint import pprint as pp

RM = motmot.RemoteMethods
sendQ = queue.Queue()

class sendGreenlet(Greenlet):

    def __init__(self, sock):
        Greenlet.__init__(self)
        self.sock = sock
        

    def _run(self):
        while True:
            msg = sendQ.get()

            sVal = msgpack.packb(msg)
            print "Sending: "
            print msg
            self.sock.sendall(sVal)


class recvGreenlet(Greenlet):

    def __init__(self, sock):
        Greenlet.__init__(self)
        self.sock = sock

    def _run(self):
        unpacker = msgpack.Unpacker()
        while True:
            rVal = self.sock.recv(4096)
            if not rVal:
                print "connection gone"
                break

            rVal = unpacker.feed(rVal)
            print "Receiving: "
            for o in unpacker:
                pp(o)

                respList = [RM.PUSH_CLIENT_STATUS, RM.PUSH_FRIEND_ACCEPT]
                if o[0] in respList:
                    sVal = [60 , "Success"]
                    print sVal
                    sendQ.put(sVal)




if __name__ == '__main__':

    address = (bSock.gethostbyname('127.0.0.1'), 8888)

    sock = socket.socket()
    sock = ssl.wrap_socket(sock)
    sock.connect(address)
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    #sendQ.put([RM.REGISTER_FRIEND, "ej2@bensing.com"])
    #sendQ.put([RM.UNREGISTER_FRIEND, "test@bensing.com"])
    #sendQ.put([RM.REGISTER_STATUS, motmot.status.AWAY])
    #sendQ.put([RM.ACCEPT_FRIEND, "ej2@bensing.com"])
    #sendQ.put([RM.REGISTER_FRIEND, "ej2@bensing22.com"])
    #sendQ.put([RM.GET_ALL_STATUSES])
    recv = recvGreenlet(sock)
    recv.start()
    send = sendGreenlet(sock)
    send.start()
    recv.join()

    """
    test = [1,3,"bob@bensing.com"]

    sock.sendall(msgpack.packb(test))

    rVal = sock.recv(4096)
    print msgpack.unpackb(rVal)

    test = [1,5,2]

    sock.sendall(msgpack.packb(test))

    rVal = msgpack.unpackb(sock.recv(4096))
    print rVal

    sock.close()

    sock2 = socket.socket()
    sock2.connect(address)


    test = [1,1,"bob@bensing.com", "12345"]

    sock2.sendall(msgpack.packb(test))

    rVal = msgpack.unpackb(sock2.recv(4096))
    print rVal


    test = [1,6,"ej@bensing.com"]

    sock2.sendall(msgpack.packb(test))

    rVal = msgpack.unpackb(sock2.recv(4096))
    print rVal
    """
