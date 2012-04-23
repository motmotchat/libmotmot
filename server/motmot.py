from gevent.server import *
from gevent import Greenlet
from gevent import socket
from gevent import monkey
import msgpack
import sqlite3 as lite
import sys
from gevent.queue import Queue
import socket as bSock
from mothelper import *

from pprint import pprint as pp # For debugging

# TODO: All the methods that used to live in motServer.py should go here

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
    AUTHENTICATED=61
    AUTH_FAILED=62
    SUCCESS=60
    ACCESS_DENIED=63


RM = RemoteMethods

DENIED = [RM.ACCESS_DENIED, "Access Denied"]

def nop(conn, *args):
    print "Message from %s:%d" % conn.address
    pp(args)
    return {'ack': args}

# this is an enum for the different statuses
class status:
    ONLINE=1
    AWAY=2
    OFFLINE=3
    BUSY=4
    SERVER=5

# generally RPC error
class RPCError(Exception):
    pass

# this is the global dictionary of currently connected, 
# authenticated clients. stored in the 
# form Key:(ipAddress, port), Value: userName
authList = {}

#authenticate a user connection, return true if valid user
def doAuth(conn, userName, password):
    con = None
    auth = False

    try:
        con = lite.connect('config.db')
        cur = con.cursor()

        cur.execute("SELECT COUNT(userId) FROM users WHERE (userName=? AND password=?);",(userName, password))

        count = cur.fetchone()

        if count[0] == 1:
            #if valid user, add them to the auth list
            authList[(conn.address[0], conn.address[1])] = [userName, status.ONLINE]
            auth = True

    except lite.Error, e:
        print "Error %s:" % e.args[0]
        sys.exit(1)
    finally:
        if con:
            con.close()

    if auth:
        return [RM.AUTHENTICATED,"Authentication Succeeded"]
    else:
        return [RM.AUTH_FAILED,"Authentication Failed"]

#when a user disconnections, removes them from the auth list
def userDisc(ipAddr, port):
    if (ipAddr, port) in authList:
        del authList[(ipAddr, port)]

# authenticate a server connection. 
# Essentially, take the domain of the server 
# and do a DNS lookup on it. If that IP equals 
# the IP that the connection is coming from, 
# the connection is authenticated
def doAuthServer(conn, hostName):
    auth = False
    ipAddr = conn.address[0]
    port = conn.address[1]

    # note: we are using the default socket package 
    # because gevent is fail and doesn't factor in /etc/hosts
    hostIp = bSock.gethostbyname(hostName)
    if hostIp == ipAddr:
        authList[(ipAddr, port)] = [hostName, status.SERVER]
        auth = True

    if auth:
        return [RM.AUTHENTICATED,"Authentication Succeeded"]
    else:
        return [RM.AUTH_FAILED,"Authentication Failed"]

# checks if a client is authenticated
def auth(conn):
    return conn.address in authList


#registers a friend request
def registerFriend(conn, friend):
    if not auth(conn):
        return DENIED

    userName = authList[conn.address][0]
    cnt_q = "SELECT COUNT(friendId) from friends WHERE userName=? AND friend=?;"
    cnt = execute_query(cnt_q, (userName, friend))
    #make sure the friend request doesn't already exist before inserting
    if cnt[0] == 0:
        ins_q = "INSERT INTO friends (userName, friend, accepted) VALUES (?, ?, 'False');"
        execute_query(ins_q, (userName, friend))

    # split the user name on @ to find the domain
    splt = friend.split("@")
    # if the domain name of the user does not match 
    # this servers name, send a request to the other server. 
    # this request is executed syncronously
    if splt[1] != conn.domain:
        # we re-use this function when register friends that come from another domain, 
        # this shit ensures that we don't get in an infinite loop of messages
        if authList[conn.address][1] != status.SERVER:
            address = (bSock.gethostbyname(splt[1]), 8888)
            sock = socket.socket()
            sock.connect(address)

            # authenticate
            sock.sendall(msgpack.packb([RM.AUTHENTICATE_SERVER, conn.domain]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[0] != RM.AUTHENTICATED:
                raise RPCError

            # send send the request
            sock.sendall(msgpack.packb([RM.SERVER_SEND_FRIEND, friend,userName]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[0] != RM.SUCESS:
                raise RPCError
            sock.close()

    else:
        # since friends are bi-directional, 
        # insert the appropriate row in the DB if the requested user is on this server
        fcnt_q = "SELECT COUNT(friendId) from friends WHERE userName=? AND friend=?;"
        cnt = execute_query(fcnt_q, (friend, userName))
        if cnt[0] == 0:
            fins_q = "INSERT INTO friends (userName, friend, accepted) VALUES (?, ?, 'False');"
            execute_query(fins_q, (friend, userName))
    
    return [RM.SUCCESS, "Friend Registered"]

#this function is an exact opposite of registerFriend and works almost identically
def unregisterFriend(conn, friend):

    if not auth(conn):
        return DENIED

    userName = authList[conn.address][0]
    del_q = "DELETE FROM friends WHERE userName=? AND friend=?;"
    execute_query(del_q, (userName, friend))

    execute_query(del_q, (friend, userName))

    splt = friend.split("@")
    if splt[1] != conn.domain:
        if authList[conn.address][1] != status.SERVER:
            address = (bSock.gethostbyname(splt[1]), 8888)
            sock = socket.socket()
            sock.connect(address)

            # authenicate
            sock.sendall(msgpack.packb([RM.AUTHENTICATE_SERVER, conn.domain]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[0] != RM.AUTHENTICATED:
                raise RPCError

            # send the request
            sock.sendall(msgpack.packb([RM.SERVER_SEND_UNFRIEND, friend, userName]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[0] != RM.SUCESS:
                raise RPCError

            sock.close()
    
    return [RM.SUCCESS, "Friend Unregistered"]
