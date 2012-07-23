from rpc import MalformedRequest

def typecheck(obj, type, message):
    if not isinstance(obj, type):
        raise MalformedRequest(message)
