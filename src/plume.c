/**
 * plume.c - Plume client.
 */

#include <glib.h>
#include <gio/gio.h>

#include "log.h"

GMainLoop *gmain;

int
plume_tls_verify_cert(GTlsConnection *conn, GTlsCertificate *cert,
    GTlsCertificateFlags errors, void *data)
{
  log_warn("plume_tls_verify_cert");

  return 1;
}

void
plume_tls_setup(GSocketClient *client, GSocketClientEvent event,
    GSocketConnectable *addr, GIOStream *conn, void *data)
{
  GTlsCertificate *cert;
  GTlsClientConnection *tls_conn;

  log_warn("plume_tls_setup: %d", event);

  if (event != G_SOCKET_CLIENT_TLS_HANDSHAKING) {
    return;
  }

  tls_conn = (GTlsClientConnection *)conn;

  cert = g_tls_certificate_new_from_file("client.crt", NULL);
  g_tls_connection_set_certificate((GTlsConnection *)tls_conn, cert);
  g_tls_client_connection_set_validation_flags(tls_conn, 0);

  g_signal_connect(tls_conn, "accept-certificate",
      (GCallback)plume_tls_verify_cert, NULL);
}

void
plume_connect_server(GObject *obj, GAsyncResult *res, void *data)
{
  GResolver *resolver;
  GSrvTarget *srv;

  GSocketConnectable *addr;
  GSocketClient *client;
  GSocketConnection *conn;

  resolver = (GResolver *)obj;
  srv = g_resolver_lookup_service_finish(resolver, res, NULL)->data;

  client = g_socket_client_new();
  g_socket_client_set_tls(client, TRUE);
  addr = g_network_address_new(g_srv_target_get_hostname(srv),
      g_srv_target_get_port(srv));

  g_signal_connect(client, "event", (GCallback)plume_tls_setup, NULL);

  conn = g_socket_client_connect(client, addr, NULL, NULL);
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
