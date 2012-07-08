import gevent
import gevent.socket
import gevent.queue
import msgpack

import rpc

class CloseConnection(gevent.GreenletExit):
    pass

class Connection():
    def __init__(self, socket, address):
        self.socket = socket
        self.address = address

        # XXX: debugging
        print("%s:%d has connected" % address)

        socket.setblocking(0)

        self.__sendqueue = gevent.queue.Queue()

        reader = gevent.spawn(self.__reader__)
        writer = gevent.spawn(self.__writer__)

        gevent.spawn(self.__reaper__, reader, writer)

    def send(self, val):
        self.__sendqueue.put(val)

    def __reader__(self):
        unpacker = msgpack.Unpacker()
        fd = self.socket.fileno()
        while True:
            gevent.socket.wait_read(fd)
            buf = self.socket.recv(4096)

            if not buf:
                raise CloseConnection()

            # TODO(carl): Limit size of buffer, force-disconnect clients that
            # send packets that are too big
            unpacker.feed(buf)

            for val in unpacker:
                rpc.dispatch(self, val)

    def __writer__(self):
        packer = msgpack.Packer()
        fd = self.socket.fileno()
        for msg in self.__sendqueue:
            buf = packer.pack(msg)
            gevent.socket.wait_write(fd)
            self.socket.sendall(buf)

    def __reaper__(self, *greenlets):
        def reap(_):
            for greenlet in greenlets:
                if greenlet.ready():
                    continue
                greenlet.unlink(reap)
                greenlet.kill(exception=CloseConnection)

        for greenlet in greenlets:
            greenlet.link(reap)
