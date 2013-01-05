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
  return plume_crypto_init();
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
  client->pc_sock_fd = -1;

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

  if (client->pc_sock_fd != -1) {
    if (close(client->pc_sock_fd) == -1) {
      log_errno("Error closing Plume server connection");
      retval = -1;
    }
  }

  free(client->pc_handle);
  free(client->pc_cert);
  free(client);

  return retval;
}

/**
 * plume_connect_server - Connect to a Plume server.
 */
void
plume_connect_server(struct plume_client *client)
{
  unsigned char *qbuf;
  int qbuflen;

  assert(client != NULL && "Attempting to connect with a null client");

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
