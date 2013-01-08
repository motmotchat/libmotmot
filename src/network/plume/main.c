/**
 * main.c - Plume test client.
 */
#include <glib.h>

#include "common/log.h"
#include "event/glib.h"
#include "plume/plume.h"

GMainLoop *gmain;

void
connected(struct plume_client *client, enum plume_status status, void *data)
{
  log_info("connect status: %d", status);
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
  channel = g_io_channel_unix_new(plume_client_get_fd(client));
  plume_client_set_data(client, channel);

  plume_client_set_connect_cb(client, connected);

  plume_connect_server(client);

  g_main_loop_run(gmain);

  return 0;
}
