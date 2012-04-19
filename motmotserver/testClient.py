#!/usr/bin/env python

from gevent import socket
from gevent import Greenlet
from gevent import queue
import msgpack
import time
import socket as bSock

class RemoteMethods:
    AUTHENTICATE_USER=1
    REGISTER_FRIEND=2
    UNREGISTER_FRIEND=3
    GET_FRIEND_IP=4
    REGISTER_STATUS=5
    AUTHENTICATE_SERVER=30
    SERVER_SEND_FRIEND=31
    SERVER_SEND_UNFRIEND=32
    ACCEPT_FRIEND=6
    SERVER_SEND_ACCEPT=34
    SERVER_SEND_STATUS_CHANGED=33
    PUSH_CLIENT_STATUS=20
    PUSH_FRIEND_ACCEPT=21
    GET_ALL_STATUSES=7
    ALL_STATUS_RESPONSE=65
    SERVER_GET_STATUS=35
    SERVER_GET_STATUS_RESP=66


sendQ = queue.Queue()

class sendGreenlet(Greenlet):

    def __init__(self, sock):
        Greenlet.__init__(self)
        self.sock = sock
        self.msgIdCnt = 0
    
    def _run(self):
        while True:
            msg = sendQ.get()
            needId = [RemoteMethods.AUTHENTICATE_USER, RemoteMethods.REGISTER_FRIEND, RemoteMethods.UNREGISTER_FRIEND, RemoteMethods.GET_FRIEND_IP, RemoteMethods.REGISTER_STATUS, RemoteMethods.ACCEPT_FRIEND, RemoteMethods.GET_ALL_STATUSES]
            if msg[1] in needId:
                self.msgIdCnt+=1
                msg[2] = ['c', self.msgIdCnt]
            
            sVal = msgpack.packb(msg)
            print "Sending: "         
            print msg
            self.sock.sendall(sVal)
            #test = [1,2,['c',self.msgIdCnt],"test@bensing.com"]


class recvGreenlet(Greenlet):

    
    def __init__(self, sock):
        Greenlet.__init__(self)
        self.sock = sock
    
    def _run(self):
        while True:
            rVal = self.sock.recv(4096)
            if not rVal:
                print "connection gone"
                break

            rVal = msgpack.unpackb(rVal)
            print "Receiving: "
            print rVal
            respList = [RemoteMethods.PUSH_CLIENT_STATUS, RemoteMethods.PUSH_FRIEND_ACCEPT]
            if rVal[1] in respList:
                sVal = [1, 60 ,rVal[2], "Success"]
                print sVal
                sendQ.put(sVal)




if __name__ == '__main__':
    
    address = (bSock.gethostbyname('127.0.0.1'), 8888)
    
    sock = socket.socket()
    sock.connect(address)
    sendQ.put([1,1,'',"ej@bensing.com","12345"])
    sendQ.put([1,2,'',"test@bensing.com"])
    sendQ.put([1,5,'',2])
    sendQ.put([1,7,''])
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
