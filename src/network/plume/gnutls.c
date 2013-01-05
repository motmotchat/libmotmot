/**
 * gnutls.c - TLS backend for Plume using GnuTLS.
 */
#ifdef USE_GNUTLS

#include <assert.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "common/log.h"
#include "crypto/gnutls.h"
#include "plume/common.h"
#include "plume/tls.h"

static const char *priorities = "SECURE256:%SERVER_PRECEDENCE";
static gnutls_priority_t priority_cache;

static int plume_tls_verify(gnutls_session_t session);


///////////////////////////////////////////////////////////////////////////////
//
//  Constructors, destructors, and setters.
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
 * Initialization amounts to allocating a credentials object and setting the
 * associated certificate verify callback.
 */
int
plume_tls_init(struct plume_client *client)
{
  assert(client != NULL);

  if (gnutls_certificate_allocate_credentials(&client->pc_tls.mt_creds)) {
    log_error("Error allocating crypto credentials");
    return 1;
  }

  gnutls_certificate_set_verify_function(client->pc_tls.mt_creds,
      plume_tls_verify);

  return 0;
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

static int
plume_tls_verify(gnutls_session_t session)
{
  return 0;
}

#endif /* USE_GNUTLS */
