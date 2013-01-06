#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "common/log.h"
#include "trill/trill.h"

GMainLoop *gmain;

struct callback {
  motmot_event_callback_t func;
  void *arg;
};

struct connection {
  struct trill_connection *conn;
  GIOChannel *chan;
};

gboolean
call_callback(void *data)
{
  int r;
  struct callback *cb = data;

  if ((r = cb->func(cb->arg))) {
    free(cb);
  }

  return r;
}

gboolean
socket_can_read(GIOChannel *source, GIOCondition cond, void *data)
{
  return call_callback(data);
}

gboolean
socket_can_write(GIOChannel *source, GIOCondition cond, void *data)
{
  return call_callback(data);
}

struct callback *
callback_new(motmot_event_callback_t func, void *arg)
{
  struct callback *cb;

  cb = malloc(sizeof(*cb));
  assert(cb != NULL);
  cb->func = func;
  cb->arg = arg;

  return cb;
}

int
want_read(int fd, enum motmot_fdtype fdtype, void *data,
    motmot_event_callback_t func, void *arg)
{
  struct connection *conn = data;

  g_io_add_watch(conn->chan, G_IO_IN, socket_can_read,
      callback_new(func, arg));

  return 0;
}

int
want_write(int fd, enum motmot_fdtype fdtype, void *data,
    motmot_event_callback_t func, void *arg)
{
  struct connection *conn = data;

  g_io_add_watch(conn->chan, G_IO_OUT, socket_can_write,
      callback_new(func, arg));

  return 0;
}

int
want_timeout(motmot_event_callback_t func, void *arg, void *data, unsigned usecs)
{
  g_timeout_add(usecs, call_callback, callback_new(func, arg));

  return 0;
}

int
main(int argc, char *argv[])
{
  struct trill_connection *conn;
  struct connection *myconn;
  char buf[25];
  char *buf_ptr;

  srandomdev();

  gmain = g_main_loop_new(g_main_context_default(), 0);

  motmot_event_init(want_read, want_write, want_timeout);
  trill_init();

  conn = trill_connection_new();
  log_info("Listening on port %d", trill_get_port(conn));

  trill_set_key(conn, "mycert.pem", "mycert.pem");
  trill_set_ca(conn, "mycert.pem");

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

  g_main_loop_run(gmain);

  return 0;
}
