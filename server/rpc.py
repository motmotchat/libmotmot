import gevent
import motmot

RM = motmot.RemoteMethods

dispatch_table = {
    RM.AUTHENTICATE_USER:           motmot.doAuth,
    RM.REGISTER_FRIEND:             motmot.registerFriend,
    RM.UNREGISTER_FRIEND:           motmot.unregisterFriend,
    RM.GET_FRIEND_IP:               motmot.nop,
    RM.REGISTER_STATUS:             motmot.nop,
    RM.AUTHENTICATE_SERVER:         motmot.nop,
    RM.SERVER_SEND_FRIEND:          motmot.nop,
    RM.SERVER_SEND_UNFRIEND:        motmot.nop,
    RM.ACCEPT_FRIEND:               motmot.nop,
    RM.SERVER_SEND_ACCEPT:          motmot.nop,
    RM.SERVER_SEND_STATUS_CHANGED:  motmot.nop,
    RM.PUSH_CLIENT_STATUS:          motmot.nop,
    RM.PUSH_FRIEND_ACCEPT:          motmot.nop,
    RM.GET_ALL_STATUSES:            motmot.nop,
    RM.SERVER_GET_STATUS:           motmot.nop,
    RM.SERVER_GET_STATUS_RESP:      motmot.nop,
    RM.ALL_STATUS_RESPONSE:         motmot.nop,
}

def writeback(conn, fn, args):
    print "writeback"
    conn.send(fn(conn, *args))

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
