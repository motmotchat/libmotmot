#!/usr/bin/env python

from gevent.server import *
from gevent import Greenlet
import msgpack
import sqlite3 as lite
import sys
import Queue

class RemoteMethods:
    AUTHENTICATE_USER=1
    REGISTER_FRIEND=2
    UNREGISTER_FRIEND=3
    GET_FRIEND_IP=4
    REGISTER_STATUS=5
    SERVER_SEND_FRIEND=31
    SERVER_SEND_UNFRIEND=32
    ACCEPT_FRIEND=6
    SERVER_SEND_ACCEPT=34
    SERVER_SEND_STATUS_CHANGED=33

class status:
    ONLINE=1
    AWAY=2
    OFFLINE=3
    BUSY=4
    
DOMAIN_NAME = "bensing.com"
authList = {}
qList = {}

def doAuth(userName, password, ipAddr, port):
    con = None
    auth = False

    try:
        con = lite.connect('config.db')
        cur = con.cursor()

        cur.execute("SELECT COUNT(userId) FROM users WHERE (userName=? AND password=?);    ",(userName, password))

        count = cur.fetchone()

        if count[0] == 1:
            authList[(ipAddr, port)] = [userName, status.ONLINE]
            auth = True

    except lite.Error, e:
        print "Error %s:" % e.args[0]
        sys.exit(1)
    finally:
        if con:
            con.close()

    return auth

    

def execute_query(q, params):
    con = None

    try:
        con = lite.connect('config.db')
        cur = con.cursor()
        
        cur.execute(q, params)
        rVal = cur.fetchone()
        con.commit()
        return rVal
    except lite.Error, e:
        print "Error %s: " % e.args[0]
    finally:
        if con:
            con.close()


def registerFriend(userName, friend, allowRemoteSend):

    cnt = execute_query("SELECT COUNT(friendId) from friends WHERE userName=? AND friend=?;", (userName, friend))
    if cnt[0] == 0:
        execute_query("INSERT INTO friends (userName, friend, accepted) VALUES (?, ?, 'False');", (userName, friend))

    splt = friend.split("@")
    if splt[1] != DOMAIN_NAME:
        if allowRemoteSend:
            address = (splt[1], 38009)
            sock = socket.socket()
            sock.connect(address)

            sock.sendall(msgpack.packb([1,31,friend,userName]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[2] != 60:
                raise RPCError
            sock.close()

    else:
        cnt = execute_query("SELECT COUNT(friendId) from friends WHERE userName=? AND friend=?;", (friend, userName))
        if cnt[0] == 0:
            execute_query("INSERT INTO friends (userName, friend, accepted) VALUES (?, ?, 'False');", (friend, userName))


def unregisterFriend(userName, friend, allowRemoteSend):

    execute_query("DELETE FROM friends WHERE userName=? AND friend=?;", (userName, friend))
    
    execute_query("DELETE FROM friends WHERE userName=? AND friend=?;", (friend, userName))
    splt = friend.split("@")
    if splt[1] != DOMAIN_NAME:
        if allowRemoteSend:
            address = (splt[1], 38009)
            sock = socket.socket()
            sock.connect(address)

            sock.sendall(msgpack.packb([1,32,friend,userName]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[2] != 60:
                raise RPCError

            sock.close()

def statusChanged(userName, status):
    con = None
    try:
        con = lite.connect('config.db')
        cur = con.cursor()
        
        cur.execute("SELECT friend FROM friends WHERE userName=? AND accepted='true';", (userName,))

        rows = cur.fetchall()

        for friend in rows:
            splt = friend[0].split('@')
            print splt
            if splt[1] == DOMAIN_NAME:
                if friend[0] in qList:
                    qList[friend[0]].put((userName, status))

            else:
                address = (splt[1], 38009)
                sock = socket.socket()
                sock.connect(address)

                sock.sendall(msgpack.packb([1, 33, friend[0], userName, status]))
                rVal = sock.recv(4096)
                rVal = msgpack.unpackb(rVal)
                if rVal[2] != 60:
                    raise RPCError

                sock.close()
    except lite.Error, e:
        print "sqlite error: %s" % e.args[0]
    finally:
        if con:
            con.close()

def acceptFriend(acceptor, friend):

    execute_query("UPDATE friends SET accepted='true' WHERE userName=? AND friend=?;", (acceptor[0],friend))
    splt = friend.split("@")
    if splt[1] == DOMAIN_NAME:
        execute_query("UPDATE friends SET accepted='true' WHERE userName=? AND friend=?;", (friend, acceptor[0]))
        if friend in qList:
            qList[friend].put(acceptor)
    else:
        address = (splt[1], 38009)
        sock = socket.socket()
        sock.connect(address)

        sock.sendall(msgpack.packb([1, 34, friend, acceptor[0], acceptor[1]]))
        rVal = sock.recv(4096)
        rVal = msgpack.unpackb(rVal)
        if rVal[2] != 60:
            raise RPCError

        sock.close()

class ReaderGreenlet(Greenlet):

    def __init__(self, socket, ipAddr, port):
        Greenlet.__init__(self)
        self.socket = socket
        self.ipAddr = ipAddr
        self.port = port

    def _run(self):
        while True:
            line = self.socket.recv(4096)
            if not line:
                del authList[(self.ipAddr, self.port)]
                print "Connection Gone"
                break

            val = msgpack.unpackb(line)
            print val

            if val[0] != 1:
                self.socket.sendall(msgpack.packb([1,91,"Invalid Version Number"]))
            else:
                if val[1] == RemoteMethods.AUTHENTICATE_USER:
                    success = doAuth(val[2], val[3], self.ipAddr, self.port)
                    if success:
                        self.socket.sendall(msgpack.packb([1,61,"Authentication Succeeded"]))
                    else:
                        self.socket.sendall(msgpack.packb([1,92,"Permission Denied"]))
                else:
                    if (self.ipAddr, self.port) in authList:
                        if val[1] == RemoteMethods.REGISTER_FRIEND:
                            registerFriend(authList[(self.ipAddr, self.port)][0], val[2], True)
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.UNREGISTER_FRIEND:
                            unregisterFriend(authList[(self.ipAddr, self.port)][0], val[2], True)
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.GET_FRIEND_IP:
                            test = "foo"
                        elif val[1] == RemoteMethods.REGISTER_STATUS:
                            authList[(self.ipAddr, self.port)][1] = val[2]
                            statusChanged(authList[(self.ipAddr, self.port)][0], val[2])
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.SERVER_SEND_FRIEND:
                            registerFriend(val[2], val[3], False)
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.SERVER_SEND_UNFRIEND:
                            unregisterFriend(val[2], val[3], False)
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.ACCEPT_FRIEND:
                            acceptFriend(authList[(self.ipAddr, self.port)], val[2])
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.SERVER_SEND_STATUS_CHANGED:
                            if val[2] in qList:
                                qList[val[2]].put((val[3], val[4]))
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.SERVER_SEND_ACCEPT:
                            if val[2] in qList:
                                qList[val[2]].put((val[3], val[4]))
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        else:
                            self.socket.sendall(msgpack.packb([1,99,"Method Not Found"]))
                    else:
                        self.socket.sendall(msgpack.packb([1,92,"Permission Denied"]))



    def __str__(self):
        return 'Reader Greenlet ({0}:{1})'.format(self.ipAddr, self.port)

class writerGreenlet(Greenlet):

    def __init__(self, sock, ipAddr, port):
        Greenlet.__init__(self)
        self.socket = socket
        self.ipAddr = ipAddr
        self.port = port

    def _run(self):
        while True:
           foo = "bar" 

    def __str__(self):
        return 'Writer Greenlet ({0}:{1})'.format(self.ipAddr,self.port)

def handleConnection(socket, address):
    print "New Connection from {0}:{1}".format(address[0], address[1])
    
    qList[(address[0], address[1])] = Queue.Queue()
    reader = ReaderGreenlet(socket, address[0], address[1])
    reader.start()


if __name__ == '__main__':
   server = StreamServer(('localhost',8888), handleConnection)
   server.serve_forever()
