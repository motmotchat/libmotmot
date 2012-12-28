/**
 * plume.c - Plume client.
 */

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "common/log.h"

#include "trill/client/request.h"

GMainLoop *gmain;

/**
 * plume_connect_peer - Initiate a connection with a peer.
 */
void
plume_connect_peer(GObject *obj, GAsyncResult *res, void *data)
{
  int fd;
  char *peer_handle, *peer_domain;
  GSocketConnection *conn;
  GIOChannel *channel;

  conn = g_socket_client_connect_finish((GSocketClient *)obj, res, NULL);
  log_assert(conn, "Could not connect to Plume server for %s", (char *)data);

  fd = g_socket_get_fd(g_socket_connection_get_socket(conn));
  channel = g_io_channel_unix_new(fd);

  assert(peer_handle = malloc(512));

  // Get a target peer from the user.
  fputs("Who do you want to connect to? ", stdout);
  fgets(peer_handle, sizeof(peer_handle), stdin);
  g_strstrip(peer_handle);

  peer_domain = strchr(peer_handle, '@');
  log_assert(peer_domain, "Peer must be specified as <name>@<domain>");
  ++peer_domain;
}

/**
 * plume_tls_verify_cert - Verify a Plume server's certificate.
 */
int
plume_tls_verify_cert(GTlsConnection *conn, GTlsCertificate *cert,
    GTlsCertificateFlags errors, void *data)
{
  return 1;
}

/**
 * plume_tls_setup - Bind our cert to the TLS handshake and enforce that the
 * handshake succeeds.
 */
void
plume_tls_setup(GSocketClient *client, GSocketClientEvent event,
    GSocketConnectable *addr, GIOStream *conn, void *data)
{
  bool *tls_flag;
  GTlsCertificate *cert;
  GTlsClientConnection *tls_conn;

  tls_flag = (bool *)data;

  switch (event) {
    case G_SOCKET_CLIENT_RESOLVING:
      *tls_flag = false;
      break;

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
      *tls_flag = true;
      break;

    case G_SOCKET_CLIENT_COMPLETE:
      log_assert(*tls_flag, "Could not establish TLS with Plume server");
      free(tls_flag);
      break;

    default:
      break;
  }
}

/**
 * plume_connect_server - Complete DNS resolution and connect to the Plume
 * server.
 */
void
plume_connect_server(GObject *obj, GAsyncResult *res, void *data)
{
  bool *tls_flag;
  GList *srv_list;
  GSrvTarget *srv;
  GSocketConnectable *addr;
  GSocketClient *client;

  // Collect SRV data.
  srv_list = g_resolver_lookup_service_finish((GResolver *)obj, res, NULL);
  log_assert(srv_list, "Could not find Plume server for %s", (char *)data);
  srv = srv_list->data;

  // Construct a client socket for the service.
  client = g_socket_client_new();
  g_socket_client_set_tls(client, TRUE);
  addr = g_network_address_new(g_srv_target_get_hostname(srv),
      g_srv_target_get_port(srv));

  // Initiate nonblocking connection.  We pass a byte of data into the signal
  // callback so that we can die if TLS fails.
  assert(tls_flag = malloc(sizeof(*tls_flag)));
  g_signal_connect(client, "event", (GCallback)plume_tls_setup, tls_flag);
  g_socket_client_connect_async(client, addr, NULL, plume_connect_peer, data);
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
    log_assert(env_home, "No $HOME set");

    strncpy(motmot_path, env_home, PATH_MAX);
    strncat(motmot_path, "/.motmot", 8);
  }

  return motmot_path;
}

int
main(int argc, char *argv[])
{
  char *handle, *domain;

  log_assert(argc == 2, "Usage: ./plume self-handle");
  g_type_init();

  // Abort if Glib doesn't have a real TLS backend.
  log_assert(g_tls_backend_supports_tls(g_tls_backend_get_default()),
      "No TLS support found; try installing the glib-networking package");

  // Pull out our own domain.
  handle = argv[1];
  domain = strchr(handle, '@');
  log_assert(domain, "Handle must be specified as <name>@<domain>");
  ++domain;

  // Look up the Plume server asynchronously.
  g_resolver_lookup_service_async(g_resolver_get_default(), "plume", "tcp",
      domain, NULL, plume_connect_server, (void *)handle);

  // Launch the main event loop.
  gmain = g_main_loop_new(g_main_context_default(), 0);
  g_main_loop_run(gmain);

  return 0;
}
