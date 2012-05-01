#!/usr/bin/env python

from gevent import socket
from gevent import ssl
from gevent import Greenlet
from gevent import queue
import msgpack
import time
import socket as bSock
import sys

import motmot
import cryptomot
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
                if o:
                    if o[0] in respList:
                        sVal = [60 , "Success"]
                        print sVal
                        sendQ.put(sVal)


# testing for the authentication functionality
def test_auth(tPass):
    if tPass=='True':
        sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    else:
        sendQ.put([RM.AUTHENTICATE_USER,"badUser","12345"])
        sendQ.put([RM.AUTHENTICATE_USER,2339290,";insert into users(userName,password) values ('owned','owned'); --"])
        sendQ.put([RM.AUTHENTICATE_USER,"badUser"])

# testing for the friend registering functionality 
def test_friend(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    if tPass=='True':
        sendQ.put([RM.REGISTER_FRIEND, "ej2@bensing.com"])
    else:
        sendQ.put([RM.REGISTER_FRIEND, "baduser@bensing.com"])
        sendQ.put([RM.REGISTER_FRIEND, ";insert into users(userName,password) values ('owned','owned'); --"])
        sendQ.put([RM.REGISTER_FRIEND, ])

# tests unfriending 
def test_unfriend(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    if tPass=='True':
        sendQ.put([RM.UNREGISTER_FRIEND, "ej2@bensing.com"])
    else:
        sendQ.put([RM.UNREGISTER_FRIEND, "baduser@bensing.com"])
        sendQ.put([RM.UNREGISTER_FRIEND, ";insert into users(userName,password) values ('owned','owned'); --"])
        sendQ.put([RM.UNREGISTER_FRIEND, ])


# tests unfriending 
def test_accept(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    sendQ.put([RM.UNREGISTER_FRIEND, "ej2@bensing.com"])
    if tPass=='True':
        sendQ.put([RM.ACCEPT_FRIEND, "ej2@bensing.com"])
    else:
        sendQ.put([RM.ACCEPT_FRIEND, "baduser@bensing.com"])
        sendQ.put([RM.ACCEPT_FRIEND, ";insert into users(userName,password) values ('owned','owned'); --"])
        sendQ.put([RM.ACCEPT_FRIEND, ])

# used for listening for push notifications to server
def test_listener(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej2@bensing.com","12345"])

# testing get all status functionality
def test_getAllStatus(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    if tPass=='True':
        sendQ.put([RM.GET_ALL_STATUSES])
    else:
        sendQ.put([RM.GET_ALL_STATUSES, "baduser@bensing.com"])
        sendQ.put([RM.GET_ALL_STATUSES, ";insert into users(userName,password) values ('owned','owned'); --"])

# testing user status function
def test_getUserStatus(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    if tPass=='True':
        sendQ.put([RM.GET_USER_STATUS,"ej2@bensing.com"])
    else:
        sendQ.put([RM.GET_USER_STATUS, "baduser@bensing.com"])
        sendQ.put([RM.GET_USER_STATUS, ";insert into users(userName,password) values ('owned','owned'); --"])
        sendQ.put([RM.GET_USER_STATUS, ])

def test_cert(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    if tPass=='True':
        cryptomot.create_self_signed_cert('test',"ej@bensing.com")
        cert = open('ctest/motmot.crt').read()
        sendQ.put([RM.SIGN_CERT_REQUEST, cert])
    else:
        sendQ.put([RM.SIGN_CERT_REQUEST, "baduser@bensing.com"])
        sendQ.put([RM.SIGN_CERT_REQUEST, ";insert into users(userName,password) values ('owned','owned'); --"])
        sendQ.put([RM.SIGN_CERT_REQUEST, ])
        cert = open('cert/motmot.crt').read()
        sendQ.put([RM.SIGN_CERT_REQUEST, cert])

def test_multiServer_sender(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing1.com","12345"])
    if tPass=='True':
        sendQ.put([RM.REGISTER_FRIEND, "ej@bensing2.com"])
        sendQ.put([RM.ACCEPT_FRIEND, "ej@bensing2.com"])
        #sendQ.put([RM.REGISTER_STATUS, motmot.status.BUSY])
        #sendQ.put([RM.GET_USER_STATUS, "ej@bensing2.com"])
        #time.sleep(5)
        #sendQ.put([RM.UNREGISTER_FRIEND, "ej@bensing2.com"])
    else:
        sendQ.put([RM.REGISTER_FRIEND, "baduser@bensing2.com"])
        sendQ.put([RM.ACCEPT_FRIEND, "baduser@bensing2.com"])
        sendQ.put([RM.REGISTER_STATUS, 99])
        sendQ.put([RM.GET_USER_STATUS, "baduser@bensing2.com"])

        sendQ.put([RM.REGISTER_FRIEND, ";insert into users(userName,password) values ('owned','owned'); --@bensing2.com"])
        sendQ.put([RM.ACCEPT_FRIEND, ";insert into users(userName,password) values ('owned','owned'); --@bensing2.com"])
        sendQ.put([RM.REGISTER_STATUS, 99])
        sendQ.put([RM.GET_USER_STATUS, ";insert into users(userName,password) values ('owned','owned'); --@bensing2.com"])


def test_multiServer_listen(tPass):
    sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing2.com","12345"])


testMap = {
    'auth':         test_auth,
    'friend':       test_friend,
    'unfriend':     test_unfriend,
    'accept':       test_accept,
    'listener':     test_listener,
    'getAllStat':   test_getAllStatus,
    'getStatus':    test_getUserStatus,
    'cert':         test_cert,
    'ms_send':      test_multiServer_sender,
    'ms_listen':    test_multiServer_listen,
}

if __name__ == '__main__':

    address = (bSock.gethostbyname(bSock.gethostbyname(sys.argv[3])), 8888)

    sock = socket.socket()
    sock = ssl.wrap_socket(sock)
    sock.connect(address)
    if sys.argv[1] in testMap:
        func = testMap[sys.argv[1]]
        func(sys.argv[2])
    #sendQ.put([RM.AUTHENTICATE_USER,"ej@bensing.com","12345"])
    #sendQ.put([RM.UNREGISTER_FRIEND, "test@bensing.com"])
    #sendQ.put([RM.REGISTER_STATUS, motmot.status.AWAY])
    #sendQ.put([RM.ACCEPT_FRIEND, "ej2@bensing.com"])
    #sendQ.put([RM.REGISTER_FRIEND, "ej2@bensing22.com"])
    #sendQ.put([RM.GET_ALL_STATUSES])
    #cert = open('cert/motmot.crt').read()
    #sendQ.put([RM.SIGN_CERT_REQUEST, cert])

    recv = recvGreenlet(sock)
    recv.start()
    send = sendGreenlet(sock)
    send.start()
    recv.join()



