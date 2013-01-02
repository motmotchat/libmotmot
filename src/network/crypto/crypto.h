/**
 * crypto.h - TLS datatypes.
 */
#ifndef __MOTMOT_CRYPTO_H__
#define __MOTMOT_CRYPTO_H__

#if defined(USE_GNUTLS)
#include "crypto/gnutls.h"
#elif defined(USE_OPENSSL)
#error "OpenSSL not implemented yet!"
#elif defined(USE_NSS)
#error "NSS not implemented yet!"
#else
#error "Unknown cryptographic library"
#endif

#endif /* __MOTMOT_CRYPTO_H__ */
