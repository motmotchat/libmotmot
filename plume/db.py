# db.py - Deals with redis. Sort of.

import gevent
import redis
import msgpack
import sys

from collections import defaultdict

# Hey look a database!
try:
    r = redis.StrictRedis(host='localhost', port=6379, db=0)
    r.get('') # A dummy key so we actually hit the database
except redis.ConnectionError:
    print "Could not connect to redis :("
    print "Aborting..."
    sys.exit(1)

# Also, a pubsub system. We're hooking into redis for the core event processing
# to keep the server itself completely stateless.
__subscriptions = defaultdict(set)

def publish(chan, obj):
    r.publish(chan, msgpack.packb(obj))

def subscribe(chan, queue):
    __subscriptions[chan].add(queue)

def unsubscribe(chan, queue):
    __subscriptions[chan].remove(queue)

def __subscription_consumer():
    pubsub = r.pubsub()
    pubsub.subscribe('*')
    for msg in pubsub.listen():
        chan = msg['channel']
        if chan in __subscriptions:
            unpacked = msgpack.unpackb(msg['data'])
            for queue in __subscriptions[chan]:
                queue.put(unpacked)

gevent.spawn(__subscription_consumer)
