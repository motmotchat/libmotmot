#ifndef __CRYPTO_H__
#define __CRYPTO_H__

#include <gnutls/gnutls.h>
#include <gnutls/dtls.h>

struct trill_crypto_identity {
  gnutls_certificate_credentials_t tci_creds;   // An X509 certificate
};

int trill_crypto_init(void);
struct trill_crypto_identity *trill_crypto_identity_new(const char *cafile,
    const char *crlfile, const char *certfile, const char *keyfile);
void trill_crypto_identity_free(struct trill_crypto_identity *id);

#endif // __CRYPTO_H__
