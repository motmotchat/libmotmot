#ifndef __CRYPTO_H__
#define __CRYPTO_H__

#include <gnutls/gnutls.h>
#include <gnutls/dtls.h>

#include "network.h"

struct trill_crypto_identity {
  gnutls_certificate_credentials_t tci_creds;   // An X509 certificate
};

struct trill_crypto_session {
  gnutls_session_t tcs_session;
  struct trill_connection *tcs_conn;
  struct trill_crypto_identity *tcs_id;
};

int trill_crypto_init(void);
void trill_crypto_deinit(void);
struct trill_crypto_identity *trill_crypto_identity_new(const char *cafile,
    const char *crlfile, const char *certfile, const char *keyfile);
void trill_crypto_identity_free(struct trill_crypto_identity *id);

int trill_crypto_handshake(struct trill_crypto_session *session);
int trill_crypto_can_read(struct trill_connection *conn);
int trill_crypto_can_write(struct trill_connection *conn);

#endif // __CRYPTO_H__
