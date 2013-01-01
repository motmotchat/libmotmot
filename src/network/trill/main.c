#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "common/log.h"
#include "trill/trill.h"

GMainLoop *gmain;

struct timeout_callback {
  struct trill_connection *conn;
  trill_callback_t cb;
};

struct connection {
  struct trill_connection *conn;
  GIOChannel *chan;
};

gboolean
socket_can_read(GIOChannel *source, GIOCondition cond, void *data)
{
  return trill_can_read(data);
}

gboolean
socket_can_write(GIOChannel *source, GIOCondition cond, void *data)
{
  return trill_can_write(data);
}

gboolean
timeout_trigger(void *data)
{
  struct timeout_callback *cb = data;
  int ret;

  ret = cb->cb(cb->conn);

  if (ret == 0) {
    free(cb);
  }

  return ret;
}

int
want_write(void *data)
{
  struct connection *conn = data;

  g_io_add_watch(conn->chan, G_IO_OUT, socket_can_write, conn->conn);

  return 0;
}

int
want_timeout(void *data, trill_callback_t fn, unsigned millis)
{
  struct connection *conn = data;
  struct timeout_callback *cb;

  cb = malloc(sizeof(*cb));
  cb->conn = conn->conn;
  cb->cb = fn;

  g_timeout_add(millis, timeout_trigger, cb);

  return 0;
}

int
main(int argc, char *argv[])
{
  struct trill_connection *conn;
  struct connection *myconn;
  char buf[25];
  char *buf_ptr;

  FILE *cert_file;
  char cert[4096];
  size_t cert_len;

  struct trill_callback_vtable cbs = {want_write, want_timeout};

  srandomdev();

  gmain = g_main_loop_new(g_main_context_default(), 0);

  trill_init(&cbs);

  conn = trill_connection_new();
  log_info("Listening on port %d", trill_get_port(conn));

  cert_file = fopen("mycert.pem", "r");
  cert_len = fread(cert, 1, 4096, cert_file);
  cert[cert_len] = '\0';
  trill_set_key(conn, cert, cert_len, cert, cert_len);
  trill_set_ca(conn, cert, cert_len);

  myconn = malloc(sizeof(*myconn));
  myconn->chan = g_io_channel_unix_new(trill_get_fd(conn));
  myconn->conn = conn;
  trill_set_data(conn, myconn);

  // Saddest parser ever
  fputs("Who do you want to connect to? ", stdout);
  fflush(stdout);
  fgets(buf, sizeof(buf), stdin);
  buf_ptr = buf;
  while (*buf_ptr != ':' && *buf_ptr != ' ' && *buf_ptr != '\0') {
    buf_ptr++;
  }
  *buf_ptr++ = '\0';

  trill_connect(conn, "testing.com", buf, atoi(buf_ptr));
  log_info("Connecting to %s on port %d", buf, atoi(buf_ptr));

  g_io_add_watch(myconn->chan, G_IO_IN, socket_can_read, conn);

  g_main_loop_run(gmain);

  return 0;
}
