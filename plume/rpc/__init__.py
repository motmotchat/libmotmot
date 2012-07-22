# rpc/__init__.py - RPC dispatcher

import gevent
from collections import defaultdict

import rpc.exceptions

def ohnoes():
    raise rpc.exceptions.UnknownOperation

__opcodes = defaultdict(ohnoes)

def remote(n):
    assert n > 0
    def inner(fn):
        __opcodes[n] = fn
        return fn
    return inner

def dispatch(conn, val):
    try:
        if not isinstance(val, list) or len(val) == 0:
            raise rpc.exceptions.MalformedRequest("Expected RPC message to " +
                "be a list containing at least one element")
        if not isinstance(val[0], int) or val[0] <= 0:
            raise rpc.exceptions.MalformedRequest("Expected RPC message to " +
                "begin with a positive integer opcode")

        __opcodes[val[0]](conn, *val[1:])
    except rpc.exceptions.RemoteException as e:
        conn.send(e.to_wire())
