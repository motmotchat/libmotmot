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
            authList[conn.address] = [userName, status.ONLINE]
            auth = True
            # add the connection to the connTbl in Connection
            # this is used for push updates
            conn.connTbl[userName] = conn

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
def userDisc(conn):
    if conn.address in authList:
        del conn.connTbl[authList[conn.address][0]]
        del authList[conn.address]
        

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
def registerFriend(conn, friend, un=None):
    if not auth(conn):
        return DENIED
    
    userName = un
    if un == None:
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
            sock.sendall(msgpack.packb([RM.SERVER_SEND_FRIEND, userName, friend]))
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
def unregisterFriend(conn, friend, un=None):

    if not auth(conn):
        return DENIED
    
    userName = un
    if un == None:
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
            sock.sendall(msgpack.packb([RM.SERVER_SEND_UNFRIEND, userName, friend]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[0] != RM.SUCESS:
                raise RPCError

            sock.close()
    
    return [RM.SUCCESS, "Friend Unregistered"]

# this is the function that handles status changes
def statusChanged(conn, status):
    if not auth(conn):
        return DENIED
    
    userName = authList[conn.address][0]
    authList[conn.address][1] = status

    con = None
    try:
        con = lite.connect('config.db')
        cur = con.cursor()
        # getting all of the user who update their status' friends
        cur.execute("SELECT friend FROM friends WHERE userName=? AND accepted='true';", (userName,))

        rows = cur.fetchall()
        # this is a list of domains that have been 
        # sent the status update already
        sentList = []
        for friend in rows:
            splt = friend[0].split('@')
            # if the friend is from the same domain, 
            # check to see if they are online and add 
            # an update message to their send Queue if they are.
            if splt[1] == conn.domain:
                if friend[0] in conn.connTbl:
                    conn.connTbl[friend[0]].send([RM.PUSH_CLIENT_STATUS, userName, status])
            
            # if they are not from the same domain, 
            # then send the update off to the appropriate domain
            else:
                if splt[1] not in sentList:
                    # again, using standard socket here because gevent doesn't 
                    # handle /etc/hosts
                    address = (bSock.gethostbyname(splt[1]), 8888)
                    sock = socket.socket()
                    sock.connect(address)

                    # doing all of this syncronously because having 
                    # persistent connections open to all servers at 
                    # all times seems a little wasteful 
                    sock.sendall(msgpack.packb([RM.AUTHENTICATE_SERVER, conn.domain]))
                    rVal = sock.recv(4096)
                    rVal = msgpack.unpackb(rVal)
                    if rVal[0] != RM.AUTHENTICATED:
                        raise RPCError

                    sock.sendall(msgpack.packb([RM.SERVER_SEND_STATUS_CHANGED, userName, status]))
                    rVal = sock.recv(4096)
                    rVal = msgpack.unpackb(rVal)
                    if rVal[0] != RM.SUCCESS:
                        raise RPCError
                    
                    sentList.append(splt[1])
                    sock.close()

    except lite.Error, e:
        print "sqlite error: %s" % e.args[0]
    finally:
        if con:
            con.close()
 
    return [RM.SUCCESS, "Status Updated"]

#accepted a friend request
def acceptFriend(conn, friend):
    if not auth(conn):
        return DENIED
    
    acceptor = authList[conn.address]
    # flip the accept bit for the user that accepted the request
    upd_q = "UPDATE friends SET accepted='true' WHERE userName=? AND friend=?;"
    execute_query(upd_q, (acceptor[0],friend))
    splt = friend.split("@")
    # if other user is on this domain, set their accept bit as well
    if splt[1] == conn.domain:
        execute_query(upd_q, (friend, acceptor[0]))
        # if the friend is online, push a notification 
        # to them that their friend request has been accepted
        if friend in conn.connTbl:
            conn.connTbl[friend].send([RM.PUSH_FRIEND_ACCEPT, acceptor[0], acceptor[1]])
    else:
        # if the user is not from this domain, send a message to the appropriate server
        address = (bSock.gethostbyname(splt[1]), 8888)
        sock = socket.socket()
        sock.connect(address)

        sock.sendall(msgpack.packb([RM.AUTHENTICATE_SERVER, conn.domain]))
        rVal = sock.recv(4096)
        rVal = msgpack.unpackb(rVal)
        if rVal[0] != RM.AUTHENTICATED:
            raise RPCError

        sock.sendall(msgpack.packb([RM.SERVER_SEND_ACCEPT, friend, acceptor[0], acceptor[1]]))
        rVal = sock.recv(4096)
        rVal = msgpack.unpackb(rVal)
        if rVal[0] != RM.SUCCESS:
            raise RPCError

        sock.close()
    
    return [RM.SUCCESS, "Friend Request Accepted"]

#get the current status for all friends, will return a list that contains online users and their statuses
def getAllFriendStatuses(conn):
    if not auth(conn):
        return DENIED
    
    userName = authList[conn.address][0]

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
            if splt[1] == conn.domain:
                if friend[0] in conn.connTbl:
                    key = conn.connTbl[friend[0]].address 
                    rList.append(authList[key])
            else:
                # we first sort the friends into lists on a domain by domain basis
                if splt[1] in frByDom:
                    frByDom[splt[1]].append(friend[0])
                else:
                    frByDom[splt[1]] = [].append(friend[0])
        
        up = msgpack.Unpacker()
        # send a message to each other domain requesting a list of statuses of the requested users
        for dom, friends in frByDom.iteritems():
            address = (bSock.gethostbyname(dom), 8888)
            sock = socket.socket()
            sock.connect(address)

            sock.sendall(msgpack.packb([RM.AUTHENTICATE_SERVER, conn.domain]))
            rVal = sock.recv(4096)
            rVal = msgpack.unpackb(rVal)
            if rVal[0] != RM.AUTHENTICATED:
                raise RPCError

            sock.sendall(msgpack.packb([RM.SERVER_GET_STATUS, friends]))
            
            up.feed(sock.recv(4096))
            
            for x in up:
                if x[0] != RM.SERVER_GET_STATUS_RESP:
                    raise RPCError
                else:
                    rList.extend(x[1])

            sock.close()

    except lite.Error, e:
        print "sqlite error: %s" % e.args[0]
    finally:
        if con:
            con.close()

    return [RM.ALL_STATUS_RESPONSE, rList]

# tells another server that a friend request has been accepted
def serverAcceptFriend(conn, userName, friend, status):
    if not auth(conn):
        return DENIED
    
    # update the accepted bit
    upd_q = "UPDATE friends SET accepted='true' WHERE userName=? AND friend=?"
    execute_query(upd_q, (userName, friend))

    # check to see if user is online, if so notify them
    if userName in conn.connTbl:
        conn.connTbl[userName].send([RM.PUSH_FRIEND_ACCEPT, friend, status])

    return [RM.SUCCESS, "Friend Accepted"]


def serverStatusChange(conn, userName, status):
    if not auth(conn):
        return DENIED
    
    # get a list of users who would be interested
    # in this update
    q = "SELECT userName FROM friends WHERE friend=? AND accepted='true'"
    con = None
    try:
        con = lite.connect('config.db')
        cur = con.cursor()

        cur.execute(q, (userName,))
        rows = cur.fetchall()

        for user in rows:
            # if user is online, push the update out
            if user[0] in conn.connTbl:
                conn.connTbl[user[0]].send([RM.PUSH_CLIENT_STATUS, userName, status])

    except lite.Error, e:
        print "sqlite error: %s" % e.args[0]
    finally:
        if con:
            con.close()

    return [RM.SUCCESS, "Status Accepted"]

# this method will return the current status of the requested users
def serverGetStatus(conn, users):
    if not auth(conn):
        return DENIED
     
    rList = []
    for user in users:
        # if the user is online, get status and add to return list
        if user in conn.connTbl:
            rList.append(authList[conn.connTbl[user].address])

    return [RM.SERVER_GET_STATUS_RESP, rList]
