#!/usr/bin/env python

import gevent
import gevent.socket
import gevent.server
import gevent.queue
import msgpack
import sys
import socket as bSock

import rpc
import cryptomot

DOMAIN_NAME = ""

class Connection():
    new = gevent.queue.Queue()
    dead = gevent.queue.Queue()

    # used to allow for push updates
    # XXX: I don't think this works as expected...
    connTbl = {}

    def __init__(self, socket, address):
        self.socket = socket
        self.address = address

        socket.setblocking(0)

        # Object queues
        self.__sendqueue = gevent.queue.Queue()
        self.__recvqueue = gevent.queue.Queue()

        # Spawn off reader-writer greenlets to process the queues
        gevent.spawn(self.__reader__)
        gevent.spawn(self.__writer__)

        # give the connection the server domain
        # this is needed for server-server auth
        self.domain = DOMAIN_NAME

        Connection.new.put(self)

    def send(self, msg, **kwargs):
        self.__sendqueue.put(msg, **kwargs)
    def recv(self, **kwargs):
        return self.__recvqueue.get(**kwargs)

    def __reader__(self):
        unpacker = msgpack.Unpacker()
        fd = self.socket.fileno()
        while True:
            gevent.socket.wait_read(fd)
            buf = self.socket.recv(4096)
            if not buf:
                return Connection.dead.push(self)
            unpacker.feed(buf)
            for val in unpacker:
                self.__recvqueue.put(val)

    def __writer__(self):
        packer = msgpack.Packer()
        fd = self.socket.fileno()
        for msg in self.__sendqueue:
            buf = packer.pack(msg)
            gevent.socket.wait_write(fd)
            self.socket.sendall(buf)

if __name__ == '__main__':
    DOMAIN_NAME = sys.argv[1]
    cryptomot.create_self_signed_cert('cert', DOMAIN_NAME)
    gevent.spawn(rpc.new_connection_watcher, Connection.new)

    server = gevent.server.StreamServer(
            (bSock.gethostbyname(DOMAIN_NAME), 8888),
            Connection,
            keyfile='cert/motmot.key',
            certfile='cert/motmot.crt')
    server.serve_forever()
