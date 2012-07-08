#!/usr/bin/env python

import gevent
import gevent.monkey
import gevent.server

gevent.monkey.patch_all()

from connection import Connection
import rpc

if __name__ == '__main__':
    server = gevent.server.StreamServer(('127.0.0.1', 8888), Connection)
    server.serve_forever()
