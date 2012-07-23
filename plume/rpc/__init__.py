# rpc/__init__.py - RPC dispatcher

import functools
import gevent
from exceptions import *

opcodes = {}

def __asyncrunner(fn, conn, *args):
    def __reaper(rpc):
        if rpc.successful(): return
        try:
            raise rpc.exception
        except RemoteException as e:
            conn.send(e.to_wire())
    gevent.spawn(fn, conn, *args).link(__reaper)

def remote(n, async=False):
    assert n > 0
    def inner(fn):
        if async:
            opcodes[n] = functools.partial(__asyncrunner, fn)
        else:
            opcodes[n] = fn
        return fn
    return inner

def __ohnoes(*args):
    """
    A catchall for all unknown opcodes
    """
    raise UnknownOperation

def dispatch(conn, val):
    """
    Dispatch a request to the given opcode handler, with a little bit of logic
    to catch malformed requests. Also, catch
    """
    if not isinstance(val, tuple) or len(val) == 0:
        raise MalformedRequest("Expected RPC message to be a list " + \
                "containing at least one element")
    if not isinstance(val[0], int) or val[0] <= 0:
        raise MalformedRequest("Expected RPC message to begin with a " + \
                "positive integer opcode")
    try:
        opcodes.get(val[0], __ohnoes)(conn, *val[1:])
    except RemoteException as e:
        conn.send(e.to_wire())

# Import all the actual modules that define actual opcodes
import user
