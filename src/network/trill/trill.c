#include <errno.h>

#include "network/trill/common.h"
#include "network/trill/crypto.h"
#include "network/trill/trill.h"

trill_want_write_callback_t trill_want_write_callback;
trill_want_timeout_callback_t trill_want_timeout_callback;

int
trill_init(const struct trill_callback_vtable *vtable)
{
  trill_want_write_callback = vtable->want_write_callback;
  trill_want_timeout_callback = vtable->want_timeout_callback;

  return trill_crypto_init();
}

int
trill_get_fd(const struct trill_connection *conn)
{
  return conn->tc_sock_fd;
}

uint16_t
trill_get_port(const struct trill_connection *conn)
{
  return conn->tc_port;
}

void
trill_set_data(struct trill_connection *conn, void *data)
{
  conn->tc_event_loop_data = data;
}

void
trill_set_connected_callback(struct trill_connection *conn,
    trill_connected_callback_t callback)
{
  conn->tc_connected_cb = callback;
}

void
trill_set_recv_callback(struct trill_connection *conn,
    trill_recv_callback_t callback)
{
  conn->tc_recv_cb = callback;
}

int
trill_can_read(struct trill_connection *conn)
{
  return conn->tc_can_read_cb(conn);
}

int
trill_can_write(struct trill_connection *conn)
{
  return conn->tc_can_write_cb(conn);
}

ssize_t
trill_send(struct trill_connection *conn, const void *data, size_t len)
{
  if (conn->tc_state != TC_STATE_ESTABLISHED) {
    errno = ENOTCONN; // XXX: is it okay to spoof errno like this?
    return -1;
  }

  return trill_tls_send(conn, data, len);
}
