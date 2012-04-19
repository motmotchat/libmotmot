#!/usr/bin/env python

from gevent.server import *
from gevent import Greenlet
from gevent import socket
from gevent import monkey
import msgpack
import sqlite3 as lite
import sys
from gevent.queue import Queue
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
    SERVER_GET_STATUS=35
    SERVER_GET_STATUS_RESP=66
    ALL_STATUS_RESPONSE=65
    

class status:
    ONLINE=1
    AWAY=2
    OFFLINE=3
    BUSY=4

class RPCError(Exception):
    pass
    
DOMAIN_NAME = "bensing.com"
authList = {}
qList = {}

def doAuth(userName, password, ipAddr, port):
    con = None
    auth = False

    try:
        con = lite.connect('config.db')
        cur = con.cursor()

        cur.execute("SELECT COUNT(userId) FROM users WHERE (userName=? AND password=?);",(userName, password))

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

def doAuthServer(hostName, ipAddr, port):
    auth = False

    hostIp = bSock.gethostbyname(hostName)
    if hostIp == ipAddr:
        authList[(ipAddr, port)] = [hostName, status.ONLINE]
        auth = True

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
            address = (bSock.gethostbyname(splt[1]), 8888)
            print "sending friend to server: "
            print address
            sock = socket.socket()
            sock.connect(address)
            
            sock.sendall(msgpack.packb([1,30,DOMAIN_NAME]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            print rVal
            if rVal[1] != 61:
                raise RPCError

            sock.sendall(msgpack.packb([1,31,friend,userName]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[1] != 60:
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
            address = (bSock.gethostbyname(splt[1]), 8888)
            sock = socket.socket()
            sock.connect(address)
            

            sock.sendall(msgpack.packb([1,30,DOMAIN_NAME]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[1] != 61:
                raise RPCError

            sock.sendall(msgpack.packb([1,32,friend,userName]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[1] != 60:
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
            if splt[1] == DOMAIN_NAME:
                if friend[0] in qList:
                    qList[friend[0]].put((userName, status))

            else:
                address = (bSock.gethostbyname(splt[1]), 8888)
                sock = socket.socket()
                sock.connect(address)

                sock.sendall(msgpack.packb([1,30,DOMAIN_NAME]))
                rVal = sock.recv(4096)
                rVal = msgpack.unpackb(rVal)
                if rVal[1] != 61:
                    raise RPCError
                
                sock.sendall(msgpack.packb([1, 33, friend[0], userName, status]))
                rVal = sock.recv(4096)
                rVal = msgpack.unpackb(rVal)
                if rVal[1] != 60:
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
        address = (bSock.gethostbyname(splt[1]), 8888)
        sock = socket.socket()
        sock.connect(address)

        sock.sendall(msgpack.packb([1,30,DOMAIN_NAME]))
        rVal = sock.recv(4096)
        rVal = msgpack.unpackb(rVal)
        if rVal[1] != 61:
            raise RPCError

        sock.sendall(msgpack.packb([1, 34, friend, acceptor[0], acceptor[1]]))
        rVal = sock.recv(4096)
        rVal = msgpack.unpackb(rVal)
        if rVal[1] != 60:
            raise RPCError

        sock.close()

def getAllFriendStatuses(userName):

    con = None
    rList = []

    try:
        con = lite.connect('config.db')
        cur = con.cursor()
        
        cur.execute("SELECT friend FROM friends WHERE userName=? AND accepted='true';", (userName,))

        rows = cur.fetchall()
        frByDom = {}
        for friend in rows:
            splt = friend[0].split('@')

            if splt[1] == DOMAIN_NAME:
                if friend[0] in qList:
                    key = [key for key, value in authList.iteritems() if value[0] == friend[0]][0]
                    rList.append(authList[key])
            else:
                if splt[1] in frByDom:
                    frByDom[splt[1]].append(friend[0])
                else:
                    frByDom[splt[1]] = [].append(friend[0])
        
        for dom, friends in frByDom.iteritems():
            address = (bSock.gethostbyname(dom), 8888)
            sock = socket.socket()
            sock.connect(address)

            sock.sendall(msgpack.packb([1,30,DOMAIN_NAME]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[1] != 61:
                raise RPCError
                
            sock.sendall(msgpack.packb([1, RemoteMethods.SERVER_GET_STATUS, friends]))

            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[1] != RemoteMethods.SERVER_GET_STATUS_RESP:
                raise RPCError
            else:
                rList.extend(rVal[2])

            sock.close()

    except lite.Error, e:
        print "sqlite error: %s" % e.args[0]
    finally:
        if con:
            con.close()

        return rList

class ReaderGreenlet(Greenlet):

    def __init__(self, socket, ipAddr, port, gl_writer):
        Greenlet.__init__(self)
        self.socket = socket
        self.ipAddr = ipAddr
        self.port = port
        self.gl_writer = gl_writer

    def _run(self):
        while True:
            line = self.socket.recv(4096)
            if not line:
                del authList[(self.ipAddr, self.port)]
                qList[authList[(self.ipAddr, self.port)][0]].put(['kill',])
                print "Connection Gone"
                break

            val = msgpack.unpackb(line)
            print val

            if val[0] != 1:
                self.socket.sendall(msgpack.packb([1,91,"Invalid Version Number"]))
            else:
                if val[1] == RemoteMethods.AUTHENTICATE_USER:
                    success = doAuth(val[3], val[4], self.ipAddr, self.port)
                    if success:
                        qList[val[3]] = Queue()
                        self.gl_writer.start()
                        qList[val[3]].put([1,61,val[2],"Authentication Succeeded"])
                    else:
                        self.socket.sendall(msgpack.packb([1,92,"Permission Denied"]))
                elif val[1] == RemoteMethods.AUTHENTICATE_SERVER:
                    success = doAuthServer(val[2], self.ipAddr, self.port)
                    if success:
                        self.gl_writer.start()
                        qList[authList[(self.ipAddr, self.port)][0]].put([1,61,val[2],"Authentication Succeeded"])
                    else:
                        self.socket.sendall(msgpack.packb([1,92,"Permission Denied"]))
                else:
                    if (self.ipAddr, self.port) in authList:
                        if val[1] == RemoteMethods.REGISTER_FRIEND:
                            registerFriend(authList[(self.ipAddr, self.port)][0], val[3], True)
                            qList[authList[(self.ipAddr, self.port)][0]].put([1,60,val[2],"Successful"])
                        elif val[1] == RemoteMethods.UNREGISTER_FRIEND:
                            unregisterFriend(authList[(self.ipAddr, self.port)][0], val[3], True)
                            qList[authList[(self.ipAddr, self.port)][0]].put([1,60,val[2],"Successful"])
                        elif val[1] == RemoteMethods.GET_FRIEND_IP:
                            test = "foo"
                        elif val[1] == RemoteMethods.REGISTER_STATUS:
                            authList[(self.ipAddr, self.port)][1] = val[3]
                            statusChanged(authList[(self.ipAddr, self.port)][0], val[3])
                            qList[authList[(self.ipAddr, self.port)][0]].put([1,60,val[2],"Successful"])
                        elif val[1] == RemoteMethods.SERVER_SEND_FRIEND:
                            registerFriend(val[2], val[3], False)
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.SERVER_SEND_UNFRIEND:
                            unregisterFriend(val[2], val[3], False)
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.ACCEPT_FRIEND:
                            acceptFriend(authList[(self.ipAddr, self.port)], val[3])
                            qList[authList[(self.ipAddr, self.port)][0]].put([1,60,val[2],"Successful"])
                        elif val[1] == RemoteMethods.SERVER_SEND_STATUS_CHANGED:
                            if val[2] in qList:
                                qList[val[2]].put([1,RemoteMethods.PUSH_CLIENT_STATUS,-1,val[3], val[4]])
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.SERVER_SEND_ACCEPT:
                            if val[2] in qList:
                                qList[val[2]].put([1,RemoteMethods.PUSH_FRIEND_ACCEPT,-1,val[3], val[4]])
                            execute_query("UPDATE friends SET accepted='true' WHERE userName=? AND friend=?;", (val[2],val[3]))
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.GET_ALL_STATUSES:
                            rStat = getAllFriendStatuses(authList[(self.ipAddr, self.port)][0])
                            qList[authList[(self.ipAddr, self.port)][0]].put([1, RemoteMethods.ALL_STATUS_RESPONSE, val[2], rStat])
                        elif val[1] == 60:
                            temp = "possibly handle return calls here"
                        else:
                            self.socket.sendall(msgpack.packb([1,99,"Method Not Found"]))
                    else:
                        self.socket.sendall(msgpack.packb([1,92,"Permission Denied"]))



    def __str__(self):
        return 'Reader Greenlet ({0}:{1})'.format(self.ipAddr, self.port)

class writerGreenlet(Greenlet):

    def __init__(self, sock, ipAddr, port):
        Greenlet.__init__(self)
        self.socket = sock
        self.ipAddr = ipAddr
        self.port = port
        self.msgIdCnt = 0

    def _run(self):
        while True:
            item = qList[authList[(self.ipAddr,self.port)][0]].get()
            if item[0] == 'kill':
                break
            needMsgId = [RemoteMethods.PUSH_CLIENT_STATUS,RemoteMethods.PUSH_FRIEND_ACCEPT]
            if item[1] in needMsgId:
                self.msgIdCnt+=1
                item[2] = ['s',self.msgIdCnt]

            print item
            self.socket.sendall(msgpack.packb(item))
            print "sent"


    def __str__(self):
        return 'Writer Greenlet ({0}:{1})'.format(self.ipAddr,self.port)

def handleConnection(socket, address):
    print "New Connection from {0}:{1}".format(address[0], address[1])
    
    writer = writerGreenlet(socket, address[0], address[1])
    reader = ReaderGreenlet(socket, address[0], address[1], writer)
    reader.start()


if __name__ == '__main__':
    DOMAIN_NAME = sys.argv[1]
    monkey.patch_all()
    server = StreamServer(('127.0.0.1',8888), handleConnection)
    server.serve_forever()
