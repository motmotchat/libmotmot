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
  return 1;
}

void
plume_tls_setup(GSocketClient *client, GSocketClientEvent event,
    GSocketConnectable *addr, GIOStream *conn, void *data)
{
  GTlsCertificate *cert;
  GTlsClientConnection *tls_conn;

  if (event == G_SOCKET_CLIENT_TLS_HANDSHAKED) {
    log_warn("TLS handshake completed");
  }

  if (event != G_SOCKET_CLIENT_TLS_HANDSHAKING) {
    return;
  }

  tls_conn = (GTlsClientConnection *)conn;

  // Bind our certificate to the connection.
  cert = g_tls_certificate_new_from_files("client.crt", "client.key", NULL);
  g_tls_connection_set_certificate((GTlsConnection *)tls_conn, cert);

  // Validate ALL the server certs!
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

  // Construct a client socket for the service.
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

  // Abort if Glib doesn't have a real TLS backend.
  if (!g_tls_backend_supports_tls(g_tls_backend_get_default())) {
    log_error("No TLS support found; try installing the "
              "glib-networking package.");
    return 1;
  }

  // Look up the Plume server.
  g_resolver_lookup_service_async(g_resolver_get_default(), "plume", "tcp",
      "mxawng.com", NULL, plume_connect_server, NULL);

  gmain = g_main_loop_new(g_main_context_default(), 0);
  g_main_loop_run(gmain);

  return 0;
}
