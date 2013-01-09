/**
 * tls.h - Plume TLS interface.
 *
 * Adding support for a new crypto library requires implementing this interface.
 */
#ifndef __PLUME_TLS_H__
#define __PLUME_TLS_H__

#include "plume/common.h"

int plume_crypto_init(void);
int plume_crypto_deinit(void);

int plume_tls_init(struct plume_client *);
int plume_tls_deinit(struct plume_client *);
char *plume_crt_get_cn(char *, size_t);

int plume_tls_start(struct plume_client *);
ssize_t plume_tls_send(struct plume_client *, const void *, size_t);

#endif /* __PLUME_TLS_H__ */
