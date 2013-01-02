/**
 * gnutls.h - TLS datatypes.
 */
#ifndef __MOTMOT_CRYPTO_GNUTLS_H__
#define __MOTMOT_CRYPTO_GNUTLS_H__

#include <gnutls/gnutls.h>

struct motmot_net_tls {
  gnutls_certificate_credentials_t mt_creds;
  gnutls_session_t mt_session;
};

int motmot_net_gnutls_init(gnutls_priority_t *, const char *);
int motmot_net_gnutls_deinit(gnutls_priority_t);

#endif /* __MOTMOT_CRYPTO_GNUTLS_H__ */
