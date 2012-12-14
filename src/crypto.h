#ifndef __TRILL_CRYPTO_H__
#define __TRILL_CRYPTO_H__

#include "common.h"
#include "network.h"

int trill_crypto_init(void);
int trill_crypto_deinit(void);

int trill_tls_init(struct trill_connection *conn);
int trill_tls_free(struct trill_connection *conn);

int trill_start_tls(struct trill_connection *conn);

int trill_tls_handshake(struct trill_connection *conn);

#endif // __TRILL_CRYPTO_H__
