import rpc
import db

OP_BUDDIES = 1

# XXX: A hack until we do peer certs
@rpc.remote(999)
def XXX_set_alias(conn, alias):
    typecheck(alias, str, "Alias must be a string")

    conn.alias = alias
    conn.send('hi %s' % alias)

@rpc.remote(1, async=True)
def get_buddy_list(conn):
    ns = 'user:%s:buddies' % conn.alias
    buddies = []
    def db_get_buddy_list(pipe):
        buddies.extend(pipe.smembers(ns))
        pipe.multi()
        for buddy in buddies:
            pipe.smembers(ns + ':' + buddy)
    groups = db.r.transaction(db_get_buddy_list, ns + ':buddies')
    conn.send((OP_BUDDIES, zip(buddies, groups)))

@rpc.remote(2, async=True)
def set_buddy_groups(conn, buddy, groups):
    """
    Set which groups a buddy is in
    """
    typecheck(buddy, str, 'Buddy must be a string')
    typecheck(groups, str, 'Groups list must be a list')
    for i, group in enumerate(groups):
        typecheck(group, str, 'Group %d must be a string' % (i+1))

    ns = 'user:%s:buddies' % conn.alias
    def db_set_buddy_groups(pipe):
        if pipe.sismember(ns, buddy):
            return
        pipe.multi()
        pipe.del(ns + ':' + buddy)
        if len(groups) > 0:
            pipe.sadd(ns + ':' + buddy, *groups)
    db.r.transaction(db_set_buddy_groups, ns)

    # This will trigger an asyncronous update, so no need to respond ourselves
    db.publish('update::user:%s:buddies:%s' % (conn.alias, buddy))

@rpc.remote(3, async=True)
def authorize_buddy(conn, buddy):
    db.r.sadd('user:%s:buddies' % conn.alias, buddy)

@rpc.remote(4, async=True)
def deauthorize_buddy(conn, buddy):
    pipe = db.r.pipeline()
    pipe.sadd('user:%s:buddies' % conn.alias, buddy)
    pipe.del('user:%s:buddies:%s' % (conn.alias, buddy))

    db.publish('delete::user:%s:buddies:%s' % (conn.alias, buddy))

@rpc.remote(5, async=True)
def update_status(conn, status):
    pass

@rpc.remote(6, async=True)
def get_statuses(conn):
    pass
