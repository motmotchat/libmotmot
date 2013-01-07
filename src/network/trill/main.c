#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "common/log.h"
#include "trill/trill.h"
#include "event/glib.h"

GMainLoop *gmain;

int
main(int argc, char *argv[])
{
  struct trill_connection *conn;
  GIOChannel *channel;
  char ip[25], *port;

  srandomdev();

  gmain = g_main_loop_new(g_main_context_default(), 0);

  motmot_event_init(want_read, want_write, want_timeout);
  trill_init();

  conn = trill_connection_new();
  log_info("Listening on port %d", trill_get_port(conn));

  trill_set_key(conn, "mycert.pem", "mycert.pem");
  trill_set_ca(conn, "mycert.pem");

  channel = g_io_channel_unix_new(trill_get_fd(conn));
  trill_set_data(conn, channel);

  fputs("Who do you want to connect to? ", stdout);
  fgets(ip, sizeof(ip), stdin);
  g_strstrip(ip);

  port = strchr(ip, ':');
  log_assert(port, "Peer must be specified as <ip>:<port>");
  *port++ = '\0';

  log_info("Connecting to %s on port %d", ip, atoi(port));
  trill_connect(conn, "testing.com", ip, atoi(port));

  g_main_loop_run(gmain);

  return 0;
}
