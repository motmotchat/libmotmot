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

# This is essentially an enum for the different OpCodes
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

# this is an enum for the different statuses
class status:
    ONLINE=1
    AWAY=2
    OFFLINE=3
    BUSY=4

# generally RPC error
class RPCError(Exception):
    pass

#domain name for this server, it is set via the first parameter when this program is called. ie: ./motServer.py "bensing.com"
DOMAIN_NAME = "bensing.com"

# this is the global dictionary of currently connected, authenticated clients. stored in the form Key:(ipAddress, port), Value: userName
authList = {}

# this is the global dictionary of write Queues. Key: userName, Value: Queue instance
qList = {}

#authenticate a user connection, return true if valid user
def doAuth(userName, password, ipAddr, port):
    con = None
    auth = False

    try:
        con = lite.connect('config.db')
        cur = con.cursor()

        cur.execute("SELECT COUNT(userId) FROM users WHERE (userName=? AND password=?);",(userName, password))

        count = cur.fetchone()

        if count[0] == 1:
            #if valid user, add them to the auth list
            authList[(ipAddr, port)] = [userName, status.ONLINE]
            auth = True

    except lite.Error, e:
        print "Error %s:" % e.args[0]
        sys.exit(1)
    finally:
        if con:
            con.close()

    return auth

# authenticate a server connection. Essentially, take the domain of the server and do a DNS lookup on it. If that IP equals the IP that the connection is coming from, the connection is authenticated
def doAuthServer(hostName, ipAddr, port):
    auth = False

    #note: we are using the default socket package because gevent is fail and doesn't factor in /etc/hosts
    hostIp = bSock.gethostbyname(hostName)
    if hostIp == ipAddr:
        authList[(ipAddr, port)] = [hostName, status.ONLINE]
        auth = True

    return auth

# this is essentially for running queries that either only have 1 return value or no return value
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

#registers a friend request
def registerFriend(userName, friend, allowRemoteSend):

    cnt = execute_query("SELECT COUNT(friendId) from friends WHERE userName=? AND friend=?;", (userName, friend))
    #make sure the friend request doesn't already exist before inserting
    if cnt[0] == 0:
        execute_query("INSERT INTO friends (userName, friend, accepted) VALUES (?, ?, 'False');", (userName, friend))

    # split the user name on @ to find the domain
    splt = friend.split("@")
    # if the domain name of the user does not match this servers name, send a request to the other server. this request is executed syncronously
    if splt[1] != DOMAIN_NAME:
        # we re-use this function when register friends that come from another domain, this parameter ensure that we don't get in an infinite loop of messages
        if allowRemoteSend:
            address = (bSock.gethostbyname(splt[1]), 8888)
            sock = socket.socket()
            sock.connect(address)
            # authenticate
            sock.sendall(msgpack.packb([1,30,DOMAIN_NAME]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[1] != 61:
                raise RPCError
            # send send the request
            sock.sendall(msgpack.packb([1,31,friend,userName]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[1] != 60:
                raise RPCError
            sock.close()

    else:
        # since friends are bi-directional, insert the appropriate row in the DB if the requested user is on this server
        cnt = execute_query("SELECT COUNT(friendId) from friends WHERE userName=? AND friend=?;", (friend, userName))
        if cnt[0] == 0:
            execute_query("INSERT INTO friends (userName, friend, accepted) VALUES (?, ?, 'False');", (friend, userName))

#this function is an exact opposite of registerFriend and works almost identically
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
# this is the function that handles status changes
def statusChanged(userName, status):
    con = None
    try:
        con = lite.connect('config.db')
        cur = con.cursor()
        # getting all of the user who update their status' friends
        cur.execute("SELECT friend FROM friends WHERE userName=? AND accepted='true';", (userName,))

        rows = cur.fetchall()

        for friend in rows:
            splt = friend[0].split('@')
            # if the friend is from the same domain, check to see if they are online and add an update message to their send Queue if they are.
            if splt[1] == DOMAIN_NAME:
                if friend[0] in qList:
                    qList[friend[0]].put([1,RemoteMethods.PUSH_CLIENT_STATUS,'',userName, status])
            # if they are not from the same domain, then send the update off to the appropriate domain
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

#accepted a friend request
def acceptFriend(acceptor, friend):
    # flip the accept bit for the user that accepted the request
    execute_query("UPDATE friends SET accepted='true' WHERE userName=? AND friend=?;", (acceptor[0],friend))
    splt = friend.split("@")
    # if other user is on this domain, set their accept bit as well
    if splt[1] == DOMAIN_NAME:
        execute_query("UPDATE friends SET accepted='true' WHERE userName=? AND friend=?;", (friend, acceptor[0]))
        # if the friend is online, push a notification to them that their friend request has been accepted
        if friend in qList:
            qList[friend].put([1, RemoteMethods.PUSH_FRIEND_ACCEPT, '', acceptor[0], acceptor[1]])
    else:
        # if the user is not from this domain, send a message to the appropriate server
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

#get the current status for all friends, will return a list that contains online users and their statuses
def getAllFriendStatuses(userName):

    con = None
    rList = []

    try:
        con = lite.connect('config.db')
        cur = con.cursor()
        # get all friends
        cur.execute("SELECT friend FROM friends WHERE userName=? AND accepted='true';", (userName,))

        rows = cur.fetchall()
        frByDom = {}
        for friend in rows:
            splt = friend[0].split('@')
            # if the user is from this domain, grab their status and add it to the list
            if splt[1] == DOMAIN_NAME:
                if friend[0] in qList:
                    key = [key for key, value in authList.iteritems() if value[0] == friend[0]][0]
                    rList.append(authList[key])
            else:
                # we first sort the friends into lists on a domain by domain basis
                if splt[1] in frByDom:
                    frByDom[splt[1]].append(friend[0])
                else:
                    frByDom[splt[1]] = [].append(friend[0])
        # send a message to each other domain requesting a list of statuses of the requested users
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

# this is the greenlet that reads all incoming requests
class ReaderGreenlet(Greenlet):

    def __init__(self, socket, ipAddr, port, gl_writer):
        Greenlet.__init__(self)
        self.socket = socket
        self.ipAddr = ipAddr
        self.port = port
        self.gl_writer = gl_writer

    def _run(self):
        while True:
            #read data
            line = self.socket.recv(4096)
            # if we get nothing, then the socket is dead. delete stuff from the approprite global lists/dictionaries
            if not line:
                del authList[(self.ipAddr, self.port)]
                qList[authList[(self.ipAddr, self.port)][0]].put(['kill',])
                print "Connection Gone"
                break
            # unpack the data
            val = msgpack.unpackb(line)
            print val
            # check the first value, which is always the version.
            if val[0] != 1:
                self.socket.sendall(msgpack.packb([1,91,"Invalid Version Number"]))
            else:
                # is the op code for authenication?
                if val[1] == RemoteMethods.AUTHENTICATE_USER:
                    success = doAuth(val[3], val[4], self.ipAddr, self.port)
                    if success:
                        # auth succeeded create the queue, start the writer greenlet
                        qList[val[3]] = Queue()
                        self.gl_writer.start()
                        qList[val[3]].put([1,61,val[2],"Authentication Succeeded"])
                    else:
                        #auth failed
                        self.socket.sendall(msgpack.packb([1,92,"Permission Denied"]))
                elif val[1] == RemoteMethods.AUTHENTICATE_SERVER:
                    success = doAuthServer(val[2], self.ipAddr, self.port)
                    if success:
                        self.gl_writer.start()
                        qList[authList[(self.ipAddr, self.port)][0]].put([1,61,val[2],"Authentication Succeeded"])
                    else:
                        self.socket.sendall(msgpack.packb([1,92,"Permission Denied"]))
                else:
                    # if the opcode was not for authenication, check to see if the client has already authenicated
                    if (self.ipAddr, self.port) in authList:
                        #massive if/else statement to handle incoming requests.... could probably use some cleaning. Important note: Server to server requests are syncronous, while client -> server are async. I may change this at some point if I get time
                        if val[1] == RemoteMethods.REGISTER_FRIEND:
                            registerFriend(authList[(self.ipAddr, self.port)][0], val[3], True)
                            qList[authList[(self.ipAddr, self.port)][0]].put([1,60,val[2],"Successful"])
                        elif val[1] == RemoteMethods.UNREGISTER_FRIEND:
                            unregisterFriend(authList[(self.ipAddr, self.port)][0], val[3], True)
                            qList[authList[(self.ipAddr, self.port)][0]].put([1,60,val[2],"Successful"])
                        elif val[1] == RemoteMethods.GET_FRIEND_IP:
                            test = "foo" #this is where stuff for NAT brokering is eventually supposed to go
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
                            # set the accepted bit for a friend request
                            execute_query("UPDATE friends SET accepted='true' WHERE userName=? AND friend=?;", (val[2],val[3]))
                            if val[2] in qList:
                                qList[val[2]].put([1,RemoteMethods.PUSH_FRIEND_ACCEPT,-1,va    l[3], val[4]])
                            self.socket.sendall(msgpack.packb([1,60,"Successful"]))
                        elif val[1] == RemoteMethods.GET_ALL_STATUSES:
                            rStat = getAllFriendStatuses(authList[(self.ipAddr, self.port)][0])
                            qList[authList[(self.ipAddr, self.port)][0]].put([1, RemoteMethods.ALL_STATUS_RESPONSE, val[2], rStat])
                        elif val[1] == 60:
                            temp = "possibly handle return calls here" #yah, so 60 is the OpCode for a successful message. so don't really need to do much here
                        else:
                            # OpCode not found
                            self.socket.sendall(msgpack.packb([1,99,"Method Not Found"]))
                    else:
                        # user doesnt have permission to execute commands
                        self.socket.sendall(msgpack.packb([1,92,"Permission Denied"]))



    def __str__(self):
        return 'Reader Greenlet ({0}:{1})'.format(self.ipAddr, self.port)

# this is the greenlet that writes data to the socket. it reads from a queue
class writerGreenlet(Greenlet):

    def __init__(self, sock, ipAddr, port):
        Greenlet.__init__(self)
        self.socket = sock
        self.ipAddr = ipAddr
        self.port = port
        self.msgIdCnt = 0

    def _run(self):
        while True:
            # grab item from queue
            item = qList[authList[(self.ipAddr,self.port)][0]].get()
            # an item that is 'kill' will be pushed onto the queue when the socket dies
            if item[0] == 'kill':
                break
            # these are the methods that need a messageId
            needMsgId = [RemoteMethods.PUSH_CLIENT_STATUS,RemoteMethods.PUSH_FRIEND_ACCEPT]
            # if the opCode is the list, incriment the identifier
            if item[1] in needMsgId:
                self.msgIdCnt+=1
                item[2] = ['s',self.msgIdCnt]

            print item
            # shipit
            self.socket.sendall(msgpack.packb(item))
            print "sent"


    def __str__(self):
        return 'Writer Greenlet ({0}:{1})'.format(self.ipAddr,self.port)

#handles new connections and starts the reading greenlet
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
