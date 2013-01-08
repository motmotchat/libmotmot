#ifndef __TRILL_CRYPTO_H__
#define __TRILL_CRYPTO_H__

#include "trill/common.h"

int trill_crypto_init(void);
int trill_crypto_deinit(void);

int trill_tls_init(struct trill_connection *);
int trill_tls_free(struct trill_connection *);

int trill_start_tls(struct trill_connection *);
ssize_t trill_tls_send(struct trill_connection *, const void *, size_t);

#endif // __TRILL_CRYPTO_H__
