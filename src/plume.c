/**
 * plume.c - Plume client.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "log.h"

GMainLoop *gmain;

/**
 * plume_tls_verify_cert - Verify a server's certificate.
 */
int
plume_tls_verify_cert(GTlsConnection *conn, GTlsCertificate *cert,
    GTlsCertificateFlags errors, void *data)
{
  return 1;
}

/**
 * plume_connect_handler - Handle events as we attempt to connect to the Plume
 * server.
 *
 * Performs two major functions:
 *    1. Binds our cert before TLS handshaking begins.
 *    2. Routes our calling card to our peer via the server once the connection
 *       completes.
 */
void
plume_connect_handler(GSocketClient *client, GSocketClientEvent event,
    GSocketConnectable *addr, GIOStream *conn, void *data)
{
  const char *handle;
  GTlsCertificate *cert;
  GTlsClientConnection *tls_conn;

  handle = (const char *)data;

  switch (event) {
    case G_SOCKET_CLIENT_TLS_HANDSHAKING:
      tls_conn = (GTlsClientConnection *)conn;

      // Bind our certificate to the connection.
      cert = g_tls_certificate_new_from_files("client.crt", "client.key", NULL);
      g_tls_connection_set_certificate((GTlsConnection *)tls_conn, cert);

      // Validate ALL the server certs!
      g_tls_client_connection_set_validation_flags(tls_conn, 0);
      g_signal_connect(tls_conn, "accept-certificate",
          (GCallback)plume_tls_verify_cert, NULL);
      break;

    case G_SOCKET_CLIENT_TLS_HANDSHAKED:
      log_warn("TLS handshake completed");
      break;

    case G_SOCKET_CLIENT_COMPLETE:
      break;

    default:
      break;
  }
}

/**
 * plume_connect_server - Catch the DNS resolver resolution signal and connect
 * to the Plume server.
 */
void
plume_connect_server(GObject *obj, GAsyncResult *res, void *data)
{
  GList *srv_list;
  GSrvTarget *srv;

  GSocketConnectable *addr;
  GSocketClient *client;
  GSocketConnection *conn;

  // Collect SRV data.
  srv_list = g_resolver_lookup_service_finish((GResolver *)obj, res, NULL);
  if (srv_list == NULL) {
    log_error("Could not find Plume server for peer %s", (char *)data);
    exit(1);
  }
  srv = srv_list->data;

  // Construct a client socket for the service.
  client = g_socket_client_new();
  g_socket_client_set_tls(client, TRUE);
  addr = g_network_address_new(g_srv_target_get_hostname(srv),
      g_srv_target_get_port(srv));

  g_signal_connect(client, "event", (GCallback)plume_connect_handler, data);
  conn = g_socket_client_connect(client, addr, NULL, NULL);
}

/**
 * motmot_home_dir - Returns a constant string giving the path of the Motmot
 * home directory.
 */
const char *
motmot_home_dir()
{
  static char *motmot_path = NULL;

  char *env_home;

  if (motmot_path == NULL) {
    motmot_path = malloc(PATH_MAX);

    env_home = getenv("HOME");
    if (env_home == NULL) {
      log_error("No $HOME set.");
      exit(1);
    }

    strncpy(motmot_path, env_home, PATH_MAX);
    strncat(motmot_path, "/.motmot", 8);
  }

  return motmot_path;
}

int
main(int argc, char *argv[])
{
  char handle[512];
  char *domain, *end;

  g_type_init();

  // Abort if Glib doesn't have a real TLS backend.
  if (!g_tls_backend_supports_tls(g_tls_backend_get_default())) {
    log_error("No TLS support found; try installing the "
              "glib-networking package.");
    return 1;
  }

  // Get a target peer from the user.
  fputs("Who do you want to connect to? ", stdout);
  fflush(stdout);
  fgets(handle, sizeof(handle), stdin);

  for (end = handle + strlen(handle) - 1; end > handle && isspace(*end); --end);
  *(end + 1) = '\0';

  // Pull out the domain.
  for (domain = handle; ;) {
    if (*domain == '\0') {
      log_error("Peer must be specified as <handle>@<domain>");
      exit(1);
    }
    if (*domain++ == '@') {
      break;
    }
  }

  // Look up the Plume server.
  g_resolver_lookup_service_async(g_resolver_get_default(), "plume", "tcp",
      domain, NULL, plume_connect_server, (void *)handle);

  // Launch the main event loop.
  gmain = g_main_loop_new(g_main_context_default(), 0);
  g_main_loop_run(gmain);

  return 0;
}
