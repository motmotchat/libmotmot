/**
 * tls.h - Plume TLS utilities.
 */
#ifndef __PLUME_TLS_H__
#define __PLUME_TLS_H__

#include "plume/common.h"

int plume_crypto_init(void);
int plume_crypto_deinit(void);

int plume_tls_init(struct plume_client *);
int plume_tls_clear(struct plume_client *);

#endif /* __PLUME_TLS_H__ */
