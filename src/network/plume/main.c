/**
 * main.c - Plume test client.
 */
#include <glib.h>

#include "common/log.h"
#include "event/glib.h"
#include "plume/plume.h"
#include "plume/request.h"

GMainLoop *gmain;

int
connected(struct plume_client *client, int status, void *data)
{
  log_info("connect status: %d", status);
  return 0;
}

int
main(int argc, char *argv[])
{
  struct plume_client *client;
  GIOChannel *channel;

  gmain = g_main_loop_new(g_main_context_default(), 0);

  motmot_event_glib_init();
  plume_init();

  client = plume_client_new("tmp/example.crt");
  plume_client_set_key(client, "tmp/example.key", "tmp/example.crt");

  channel = g_io_channel_unix_new(plume_client_get_fd(client));
  plume_client_set_data(client, channel);
  plume_client_set_connect_cb(client, connected);

  plume_connect_server(client);

  g_main_loop_run(gmain);

  return 0;
}
