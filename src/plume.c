/**
 * plume.c - Plume client.
 */

#include <glib.h>
#include <gio/gio.h>

#include "log.h"

GMainLoop *gmain;

void
plume_connect_server(GObject *resolver, GAsyncResult *res, void *data)
{
  GList *srvs;

  srvs = g_resolver_lookup_service_finish((GResolver *)resolver, res, NULL);

  log_info("%s:%hu", g_srv_target_get_hostname(srvs->data),
      g_srv_target_get_port(srvs->data));
}

int
main(int argc, char *argv[])
{
  g_type_init();

  g_resolver_lookup_service_async(g_resolver_get_default(), "plume", "tcp",
      "mxawng.com", NULL, plume_connect_server, NULL);

  gmain = g_main_loop_new(g_main_context_default(), 0);
  g_main_loop_run(gmain);

  return 0;
}
