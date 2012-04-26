import gevent
import motmot
import socket as bSock

RM = motmot.RemoteMethods

dispatch_table = {
    RM.AUTHENTICATE_USER:           motmot.doAuth,
    RM.REGISTER_FRIEND:             motmot.registerFriend,
    RM.UNREGISTER_FRIEND:           motmot.unregisterFriend,
    RM.GET_FRIEND_IP:               motmot.nop,
    RM.REGISTER_STATUS:             motmot.statusChanged,
    RM.AUTHENTICATE_SERVER:         motmot.doAuthServer,
    RM.SERVER_SEND_FRIEND:          motmot.registerFriend,
    RM.SERVER_SEND_UNFRIEND:        motmot.unregisterFriend,
    RM.ACCEPT_FRIEND:               motmot.acceptFriend,
    RM.SERVER_SEND_ACCEPT:          motmot.serverAcceptFriend,
    RM.SERVER_SEND_STATUS_CHANGED:  motmot.serverStatusChange,
    RM.GET_ALL_STATUSES:            motmot.getAllFriendStatuses,
    RM.SERVER_GET_STATUS:           motmot.serverGetStatus,
    RM.SUCCESS:                     motmot.nop,
}

def writeback(conn, fn, args):
    print "writeback"
    rVal = []
    # separated the errors out incase we need to do separate handling later
    try:
        rVal = fn(conn, *args)
    except bSock.gaierror, e:
        print "Exception Occured on connection from {0}:{1}".format(conn.address[0], conn.address[1])
        rVal = [RM.FRIEND_SERVER_DOWN,"Connection to Friend Server could not be made"]
    except bSock.error, e:
        print "Exception Occured on connection from {0}:{1}".format(conn.address[0], conn.address[1])
        rVal = [RM.FRIEND_SERVER_DOWN,"Connection to Friend Server could not be made"]
    except motmot.RPCError, e:
        print "Exception Occured on connection from {0}:{1}".format(conn.address[0], conn.address[1])
        rVal = [RM.FRIEND_SERVER_DOWN,"Connection to Friend Server could not be made"]
    finally:
        conn.send(rVal)

def rpc_dispatcher(conn):
    while True:
        o = conn.recv()
        print "Hello"
        try:
            if o[0] in dispatch_table:
                fn = dispatch_table[o[0]]
                # Asynchronously schedule a socket writeback
                gevent.spawn(writeback, conn, fn, o[1:])
            else:
                print "Unknown opcode %d" % o[0]
        except TypeError:
            print "Malformed message"

def new_connection_watcher(queue):
    while True:
        conn = queue.get()
        print 'New connection from %s:%d' % conn.address
        gevent.spawn(rpc_dispatcher, conn)
