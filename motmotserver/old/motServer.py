#!/usr/bin/env python
# coding: utf-8

"""Motmot Directory Server
This server using msgpackrpc.Server.
"""

import msgpackrpc
import sqlite3 as lite

class MotServerHandler(object):
    
    def __init__(self):
        self.safeList = []
        self.userList = {}

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
        
        con = None
        auth = False
        try:
            con = lite.connect('config.db')
            cur = con.cursor()

            cur.execute("SELECT COUNT(userId) FROM users WHERE (userName=? AND password=?);",(userName, password))

            count = cur.fetchone()
            if count[0] == 1:
                self.safeList.append((addr, port))
                self.userList.append(userName, (addr, port))
                auth = True
            
        except lite.Error, e:
            print "Error %s:" % e.args[0]
            sys.exit(1)
        finally:
            if con:
                con.close()

        return auth

    def registerFriend(self, friendName):
        con = None
        try:
            con = lite.connect('config.db')
            cur = con.cursor()


        
       

if __name__ == '__main__':
    server = msgpackrpc.Server(MotServerHandler())
    server.listen(msgpackrpc.Address("localhost", 8888))
    server.start()
