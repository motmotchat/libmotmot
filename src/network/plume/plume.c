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
#include "plume/plume.h"
#include "plume/common.h"
#include "plume/crypto.h"

/**
 * plume_init - Initialize the Plume client service.
 */
int
plume_init()
{
  return plume_crypto_init();
}

/**
 * plume_client_new - Instantiate a new Plume client object.
 */
struct plume_client *
plume_client_new()
{
  struct plume_client *client;

  client = calloc(1, sizeof(*client));
  if (client == NULL) {
    return NULL;
  }

  if (plume_tls_init(client)) {
    free(client);
    return NULL;
  }

  return client;
}

/**
 * plume_client_destroy - Destroy a Plume client object, closing the server
 * connection if it is still live.
 */
int
plume_client_destroy(struct plume_client *client)
{
  int r = 0;

  assert(client != NULL && "Attempting to free a null client");

  if (client->pc_sock_fd != -1) {
    if ((r = close(client->pc_sock_fd) == -1)) {
      log_errno("Error closing Plume server connection");
    }
  }

  free(client);
  return r;
}

/**
 * plume_connect_server - Connect to a Plume server.
 */
void
plume_connect_server(struct plume_client *client, const char *cert_path)
{
  unsigned char *qbuf;
  int qbuflen;

  assert(client != NULL && "Attempting to connect with a null client");
  assert(cert_path != NULL && "Attempting to connect with null credentials");

  client->pc_cert_path = strdup(cert_path);

  ares_mkquery("blah", ns_c_in, ns_t_srv, 0, 1, &qbuf, &qbuflen);
}


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

int
plume_client_get_fd(const struct plume_client *client)
{
  return client->pc_sock_fd;
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
