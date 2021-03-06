/**
 * gnutls.c - TLS backend for Plume using GnuTLS.
 */
#ifdef USE_GNUTLS

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "common/log.h"
#include "crypto/gnutls.h"
#include "plume/common.h"
#include "plume/tls.h"

static const char *priorities = "SECURE256:%SERVER_PRECEDENCE";
static gnutls_priority_t priority_cache;

static int plume_tls_verify(gnutls_session_t);


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

/**
 * plume_crypto_init - Wrapper for GnuTLS global init.
 */
int
plume_crypto_init(void)
{
  return motmot_net_gnutls_init(&priority_cache, priorities);
}

/**
 * plume_crypto_deinit - Wrapper for GnuTLS global deinit.
 */
int
plume_crypto_deinit(void)
{
  return motmot_net_gnutls_deinit(priority_cache);
}

/**
 * plume_tls_init - Initialize the motmot_net_tls component of a Plume client.
 *
 * Initialization includes the following:
 * - Allocating a credentials object.
 * - Setting the associated certificate verify callback.
 */
int
plume_tls_init(struct plume_client *client)
{
  assert(client != NULL);

  if (gnutls_certificate_allocate_credentials(&client->pc_tls.mt_creds)) {
    log_error("Error allocating crypto credentials");
    return -1;
  }
  gnutls_certificate_set_verify_function(client->pc_tls.mt_creds,
      plume_tls_verify);

  return 0;
}

/**
 * plume_tls_deinit - Free up a motmot_net_tls object.
 */
int
plume_tls_deinit(struct plume_client *client)
{
  assert(client != NULL);

  gnutls_bye(client->pc_tls.mt_session, GNUTLS_SHUT_RDWR);

  gnutls_deinit(client->pc_tls.mt_session);
  gnutls_certificate_free_credentials(client->pc_tls.mt_creds);

  return 0;
}

/**
 * plume_crt_get_cn - Extract the CN from a cert.
 */
char *
plume_crt_get_cn(char *raw_cert, size_t size)
{
  gnutls_datum_t data;
  gnutls_x509_crt_t cert;
  char *buf = NULL;
  size_t buf_size = 0;

  data.data = (unsigned char *)raw_cert;
  data.size = size;

  if (gnutls_x509_crt_init(&cert)) {
    return NULL;
  }

  if (gnutls_x509_crt_import(cert, &data, GNUTLS_X509_FMT_PEM)) {
    goto leave;
  }

  if (gnutls_x509_crt_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME,
        0, 0, NULL, &buf_size) != GNUTLS_E_SHORT_MEMORY_BUFFER) {
    goto leave;
  }

  buf = malloc(buf_size + 1);
  if (buf == NULL) {
    goto leave;
  }

  if (gnutls_x509_crt_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME,
        0, 0, buf, &buf_size)) {
    free(buf);
    buf = NULL;
  }

leave:
  gnutls_x509_crt_deinit(cert);
  return buf;
}

/**
 * plume_client_set_key - Associate a private key and cert with the client.
 */
int
plume_client_set_key(struct plume_client *client, const char *key_path,
    const char *cert_path)
{
  assert(client != NULL);
  assert(key_path != NULL);
  assert(cert_path != NULL);

  return gnutls_certificate_set_x509_key_file(client->pc_tls.mt_creds,
      cert_path, key_path, GNUTLS_X509_FMT_PEM);
}

/**
 * plume_client_set_ca - Set a trusted CA for the client.
 */
int
plume_client_set_ca(struct plume_client *client, const char *ca_path)
{
  assert(client != NULL);
  assert(ca_path != NULL);

  return gnutls_certificate_set_x509_trust_file(client->pc_tls.mt_creds,
      ca_path, GNUTLS_X509_FMT_PEM);
}


///////////////////////////////////////////////////////////////////////////////
//
//  Handshake protocol.
//

static int plume_tls_retry_read(void *);
static int plume_tls_retry_write(void *);

static int plume_tls_can_recv(void *);

/**
 * plume_tls_handshake - Wrapper around motmot_net_gnutls_handshake that
 * just performs success/failure handling and returns the status code.
 *
 * Nothing is done on RETRY_READ or RETRY_WRITE.
 */
static enum motmot_gnutls_status
plume_tls_handshake(struct plume_client *client)
{
  int r;

  assert(client != NULL);

  r = motmot_net_gnutls_handshake(&client->pc_tls);

  switch (r) {
    case MOTMOT_GNUTLS_SUCCESS:
      // Update state and set a new, permanent can-read handler.
      client->pc_state = PLUME_STATE_CONNECTED;
      plume_want_read(client, plume_tls_can_recv);
      plume_connected(client, PLUME_SUCCESS);
      break;
    case MOTMOT_GNUTLS_FAILURE:
      plume_connected(client, PLUME_ETLS);
      break;
    case MOTMOT_GNUTLS_RETRY_READ:
    case MOTMOT_GNUTLS_RETRY_WRITE:
      break;
  }

  return r;
}

/**
 * plume_tls_start - Start a TLS session and begin handshaking.
 */
int
plume_tls_start(struct plume_client *client)
{
  int r;

  assert(client != NULL);

  if ((r = motmot_net_gnutls_start(&client->pc_tls, GNUTLS_CLIENT,
          client->pc_fd, priority_cache, (void *)client))) {
    return r;
  }

  switch (plume_tls_handshake(client)) {
    case MOTMOT_GNUTLS_SUCCESS:
      return 0;
    case MOTMOT_GNUTLS_FAILURE:
      return 1;
    case MOTMOT_GNUTLS_RETRY_READ:
      return plume_want_read(client, plume_tls_retry_read);
    case MOTMOT_GNUTLS_RETRY_WRITE:
      return plume_want_write(client, plume_tls_retry_write);
  }

  // This should never happen.
  return 1;
}

/**
 * plume_tls_retry_read - Retry a handshake read.
 */
static int
plume_tls_retry_read(void *arg)
{
  switch (plume_tls_handshake((struct plume_client *)arg)) {
    case MOTMOT_GNUTLS_RETRY_READ:
      return 1;
    case MOTMOT_GNUTLS_RETRY_WRITE:
      plume_want_write((struct plume_client *)arg, plume_tls_retry_write);
    case MOTMOT_GNUTLS_SUCCESS:
    case MOTMOT_GNUTLS_FAILURE:
      break;
  }

  return 0;
}

/**
 * plume_tls_retry_read - Retry a handshake write.
 */
static int
plume_tls_retry_write(void *arg)
{
  switch (plume_tls_handshake((struct plume_client *)arg)) {
    case MOTMOT_GNUTLS_RETRY_WRITE:
      return 1;
    case MOTMOT_GNUTLS_RETRY_READ:
      plume_want_read((struct plume_client *)arg, plume_tls_retry_read);
    case MOTMOT_GNUTLS_SUCCESS:
    case MOTMOT_GNUTLS_FAILURE:
      break;
  }

  return 0;
}

static int
plume_tls_verify(gnutls_session_t session)
{
  return 0;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Transport functions.
//

/**
 * plume_tls_send - Send a message over TLS.
 */
ssize_t
plume_tls_send(struct plume_client *client, const void *data, size_t len)
{
  assert(client != NULL);
  assert(client->pc_state == PLUME_STATE_CONNECTED);

  return motmot_net_gnutls_send(&client->pc_tls, client->pc_data, data, len);
}

/**
 * plume_tls_recv - Receive data and pass it to the user.
 */
static int
plume_tls_can_recv(void *arg)
{
  struct plume_client *client;

  client = (struct plume_client *)arg;

  assert(client != NULL);
  assert(client->pc_state == PLUME_STATE_CONNECTED);

  if (client->pc_can_recv_cb != NULL) {
    client->pc_can_recv_cb(client, PLUME_SUCCESS, client->pc_data);
  }

  return 1;
}

/**
 * plume_recv - Receive data from the server over TLS.
 */
ssize_t
plume_recv(struct plume_client *client, void *buf, size_t len)
{
  ssize_t bytes_read;

  bytes_read = gnutls_record_recv(client->pc_tls.mt_session, buf, len);
  // XXX: Do some error-handling and set errno.

  return bytes_read;
}

#endif /* USE_GNUTLS */
