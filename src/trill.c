#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "log.h"
#include "network.h"

GMainLoop *gmain;

gboolean
socket_can_read(GIOChannel *source, GIOCondition cond, void *data)
{
  struct trill_connection *conn = data;

  return conn->tc_can_read_cb(conn);
}

gboolean
socket_can_write(GIOChannel *source, GIOCondition cond, void *data)
{
  struct trill_connection *conn = data;

  return conn->tc_can_write_cb(conn);
}

gboolean
timeout_trigger(void *data)
{
  struct trill_connection *conn = data;

  return conn->tc_timeout_cb(conn);
}

int
want_write(struct trill_connection *conn)
{
  g_io_add_watch(conn->tc_event_loop_descriptor, G_IO_OUT, socket_can_write,
      conn);

  return 0;
}

int
want_timeout(struct trill_connection *conn, unsigned millis)
{
  g_timeout_add(millis, timeout_trigger, conn);

  return 0;
}

int
main(int argc, char *argv[])
{
  GIOChannel *chan;
  struct trill_connection *conn;
  char buf[25];
  char *buf_ptr;

  gmain = g_main_loop_new(g_main_context_default(), 0);

  trill_net_init(want_write, want_timeout);

  conn = trill_connection_new();
  log_info("Listening on port %d", conn->tc_port);

  chan = g_io_channel_unix_new(conn->tc_sock_fd);
  conn->tc_event_loop_descriptor = chan;

  // Saddest parser ever
  fputs("Who do you want to connect to? ", stdout);
  fflush(stdout);
  fgets(buf, sizeof(buf), stdin);
  buf_ptr = buf;
  while (*buf_ptr != ':' && *buf_ptr != ' ' && *buf_ptr != '\0') {
    buf_ptr++;
  }
  *buf_ptr++ = '\0';

  trill_connection_connect(conn, buf, atoi(buf_ptr));
  log_info("Connecting to %s on port %d", buf, atoi(buf_ptr));

  g_io_add_watch(chan, G_IO_IN, socket_can_read, conn);

  g_main_loop_run(gmain);

  return 0;
}