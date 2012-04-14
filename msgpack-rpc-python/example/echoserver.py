#!/usr/bin/env python
# coding: utf-8

"""Echo service.
This server using msgpackrpc.Server.
"""

import msgpackrpc

class EchoHandler(object):
    
    def __init__(self):
        self.safeList = []

    def echo(self, msg, addr, port):
        return msg

    def validate(self, methodName, addr):
        if methodName == "authenticate":
            return True
        else:
            if self.safeList.count(addr) > 0:
                return True
            else:
                return False

    def authenticate(self, userName, password, addr, port):
        self.safeList.append((addr, port))
        

def serve_background(server, daemon=False):
    def _start_server(server):
        server.start()
        server.close()

    import threading
    t = threading.Thread(target=_start_server, args = (server,))
    t.setDaemon(daemon)
    t.start()
    return t

def serve(daemon=False):
    """Serve echo server in background on localhost.
    This returns (server, port). port is number in integer.

    To stop, use ``server.shutdown()``
    """
    for port in xrange(8888, 8890):
        try:
            addr = msgpackrpc.Address('localhost', port)
            server = msgpackrpc.Server(EchoHandler())
            print server
            server.listen(addr)
            thread = serve_background(server, daemon)
            return (addr, server, thread)
        except Exception as err:
            print err
            pass

if __name__ == '__main__':
    port = serve(False)
    print "Serving on localhost:%d\n" % port[0].port

