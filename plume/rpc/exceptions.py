class RemoteException(Exception):
    """
    Superclass of all RPC-related exceptions. Generates a msgpack-serializable
    copy of itself when to_wire is called.

    Normally, an exception is a negative opcode followed by a human-readable
    error message:
    >>> RemoteException(-1, "Something has gone terribly wrong").to_wire()
    (-1, "Something has gone terribly wrong")

    The opcode must be a negative integer so the client can tell exceptional
    error cases from non-exceptional ones:
    >>> RemoteException(1, "Something is going alright?!")
    Traceback (most recent call last):
        ...
    ValueError: Opcode must be negative

    Some remote exceptions are fatal. If a fatal exception is encountered, the
    connection to the client is considered to be in a bad state, and must be
    immediately terminated. It is the responsibility of the RPC driver to
    examine the fatal flag and abort the connection.
    """
    def __init__(self, op, msg, fatal=False):
        if op >= 0:
            raise ValueError("Opcode must be negative")
        if not float(op).is_integer():
            raise ValueError("Opcode must be an integer")

        self.op = op
        self.msg = msg
        self.fatal = fatal

    def to_wire(self):
        return (self.op, self.msg)

    def __str__(self):
        _str = "%s(%d): %s" % (self.__class__.name, self.op, self.msg)
        if self.fatal:
            return "[FATAL] %s" % _str
        return _str

def MalformedRequest(RemoteException):
    """
    Thrown iff the RPC message deserialized into an unexpected form.
    """
    def __init__(self, additional=""):
        super(-1, "There was an error with the protocol, and we were unable " +
            "to read your request.%s" % (' ' + additional), fatal=True)

def UnknownOperation(RemoteException):
    """
    Thrown iff the protocol format appears to be correct, but the opcode that
    was requested cannot be found
    """
    def __init__(self):
        super(-2, "We did not recognize the opcode you submitted.", fatal=True)
