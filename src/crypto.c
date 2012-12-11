#include <assert.h>
#include <stdlib.h>

#include "crypto.h"
#include "log.h"

// Our priorities set. Any 256-bit, secure cipher that is compatible with
// DTLS1.0 is fine, and should be secure for the foreseable future.
static const char *priorities =
  "SECURE256:-VERS-TLS-ALL:+VERS-DTLS1.0:%SERVER_PRECEDENCE";
static gnutls_priority_t priority_cache;

int
trill_crypto_init(void)
{
  if (gnutls_global_init()) {
    log_error("Error initializing gnutls");
    return 1;
  }

  gnutls_priority_init(&priority_cache, priorities, NULL);

  return 0;
}

struct trill_crypto_identity *
trill_crypto_identity_new(const char *cafile, const char *crlfile,
    const char *certfile, const char *keyfile)
{
  struct trill_crypto_identity *id;

  assert(certfile != NULL && "You must provide a certificate");
  assert(keyfile != NULL && "You must provide a private key");

  id = malloc(sizeof(*id));

  if (gnutls_certificate_allocate_credentials(&id->tci_creds)) {
    log_error("Error allocating crypto credentials");
    free(id);
    return NULL;
  }

  if (cafile != NULL) {
    if (gnutls_certificate_set_x509_trust_file(id->tci_creds, cafile,
          GNUTLS_X509_FMT_PEM) < 0) {
      log_error("Error setting x509 CA file");
      trill_crypto_identity_free(id);
      return NULL;
    }
  }

  if (crlfile != NULL) {
    if (gnutls_certificate_set_x509_crl_file(id->tci_creds, crlfile,
          GNUTLS_X509_FMT_PEM) < 0) {
      log_error("Error setting x509 CRL file");
      trill_crypto_identity_free(id);
      return NULL;
    }
  }

  if (gnutls_certificate_set_x509_key_file(id->tci_creds, certfile, keyfile,
        GNUTLS_X509_FMT_PEM)) {
    log_error("Error setting x509 keys");
    trill_crypto_identity_free(id);
    return NULL;
  }

  return id;
}

void
trill_crypto_identity_free(struct trill_crypto_identity *id)
{
  assert(id != NULL && "Attempting to free a null identity");

  gnutls_certificate_free_credentials(id->tci_creds);
  free(id->tci_creds);
}
