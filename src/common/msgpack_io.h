/**
 * msgpack_io.c - Buffered msgpack network IO.
 */
#ifndef __MOTMOT_MSGPACK_IO_H__
#define __MOTMOT_MSGPACK_IO_H__

#include <glib.h>
#include <msgpack.h>

struct msgpack_conn;

// Callback for processing received msgpack objects.  Should return zero on
// success, nonzero on error.
typedef int (*msgpack_conn_recv_t)(struct msgpack_conn *,
    const msgpack_object *);

// Callback for dropping a closed connection.
typedef void (*msgpack_conn_drop_t)(struct msgpack_conn *);

struct msgpack_conn *msgpack_conn_new(GIOChannel *, msgpack_conn_recv_t,
    msgpack_conn_drop_t);
void msgpack_conn_destroy(struct msgpack_conn *);
int msgpack_conn_send(struct msgpack_conn *, const char *, size_t);

#endif /* __MOTMOT_MSGPACK_IO_H__ */
