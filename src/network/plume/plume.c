/**
 * plume.c - Plume client interface.
 */

#include <assert.h>
#include <arpa/nameser.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ares.h>

#include "common/log.h"
#include "common/readfile.h"

#include "plume/plume.h"
#include "plume/common.h"
#include "plume/tls.h"

/**
 * plume_init - Initialize the Plume client service.
 */
int
plume_init()
{
  int r;

  if ((r = plume_crypto_init())) {
    return r;
  }

  if ((r = ares_library_init(ARES_LIB_INIT_ALL))) {
    log_error("Error initializing c-ares");
    return r;
  }

  return 0;
}

/**
 * plume_client_new - Instantiate a new Plume client object.
 */
struct plume_client *
plume_client_new(const char *cert_path)
{
  struct plume_client *client;

  assert(cert_path != NULL && "No identity cert specified for new client.");

  client = calloc(1, sizeof(*client));
  if (client == NULL) {
    return NULL;
  }
  client->pc_fd = -1;

  client->pc_cert = readfile(cert_path, &client->pc_cert_size);
  if (client->pc_cert == NULL) {
    goto err;
  }

  client->pc_handle = plume_crt_get_cn(client->pc_cert, client->pc_cert_size);
  if (client->pc_handle == NULL) {
    goto err;
  }

  if (plume_tls_init(client)) {
    goto err;
  }

  return client;

err:
  plume_client_destroy(client);
  return NULL;
}

/**
 * plume_client_destroy - Destroy a Plume client object, closing the server
 * connection if it is still live.
 */
int
plume_client_destroy(struct plume_client *client)
{
  int retval = 0;

  assert(client != NULL && "Attempting to free a null client");

  retval = plume_tls_deinit(client);

  if (client->pc_fd != -1) {
    if (close(client->pc_fd) == -1) {
      log_errno("Error closing Plume server connection");
      retval = -1;
    }
  }

  free(client->pc_handle);
  free(client->pc_cert);
  free(client);

  return retval;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Connect protocol.
//
//  The Plume server connection protocol is a multi-step asynchronous process,
//  which entails:
//
//  1.  SRV DNS lookup.
//  2.  SRV DNS resolution and server DNS lookup.
//  3.  Server DNS resolution and TCP connection.
//  4.  TLS handshaking.
//

void plume_dns_lookup(void *, int, int, unsigned char *, int);

/**
 * plume_connect_server - Begin connecting to the client's Plume server.
 */
void
plume_connect_server(struct plume_client *client)
{
  char *domain;
  unsigned char *qbuf;
  int qbuflen;
  ares_channel channel;

  assert(client != NULL && "Attempting to connect with a null client");

  if (client->pc_fd != -1) {
    client->pc_connect(client, PLUME_EINUSE, client->pc_data);
  }

  // Pull the domain from the client's handle.
  domain = strchr(client->pc_handle, '@');
  if (domain == NULL) {
    client->pc_connect(client, PLUME_EIDENTITY, client->pc_data);
    return;
  }
  ++domain;

  // Start a DNS lookup.
  // XXX: Look up the right thing.
  ares_mkquery(domain, ns_c_in, ns_t_srv, 0, 1, &qbuf, &qbuflen);
  ares_send(channel, qbuf, qbuflen, plume_dns_lookup, client);
  ares_free_string(qbuf);
}

void plume_dns_lookup(void *data, int status, int timeouts,
    unsigned char *abuf, int alen)
{
  struct plume_client *client;

  client = (struct plume_client *)data;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

int
plume_client_get_fd(const struct plume_client *client)
{
  return client->pc_fd;
}

void
plume_client_set_data(struct plume_client *client, void *data)
{
  client->pc_data = data;
}

void
plume_client_set_connect_cb(struct plume_client *client,
    plume_callback_t connect)
{
  client->pc_connect = connect;
}

void
plume_client_set_recv_cb(struct plume_client *client,
    plume_recv_callback_t recv)
{
  client->pc_recv = recv;
}
