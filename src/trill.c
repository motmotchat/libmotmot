#include "common.h"
#include "crypto.h"
#include "trill.h"

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
