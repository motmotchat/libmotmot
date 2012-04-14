#!/usr/bin/env python
# coding: utf-8

"""Motmot Directory Server
This server using msgpackrpc.Server.
"""

import msgpackrpc

class MotServerHandler(object):
    
    def __init__(self):
        self.safeList = []

    def echo(self, msg, addr, port):
        return msg

    def validate(self, methodName, addr):
        if methodName == "authenticate":
            return True
        else:
            if self.safeList.count(addr) > 0:
                return True
            else:
                return False

    def authenticate(self, userName, password, addr, port):
        print "Username", userName
        print "Password", password
        self.safeList.append((addr, port))
       

if __name__ == '__main__':
    server = msgpackrpc.Server(MotServerHandler())
    server.listen(msgpackrpc.Address("localhost", 8888))
    server.start()
