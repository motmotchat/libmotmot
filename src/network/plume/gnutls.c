/**
 * gnutls.c - TLS backend for Plume using GNUTLS.
 */
#ifdef USE_GNUTLS

#include <gnutls/gnutls.h>
#include <gnutls/dtls.h>

#include "common/log.h"
#include "crypto/gnutls.h"
#include "plume/common.h"
#include "plume/crypto.h"

static const char *priorities = "SECURE256:%SERVER_PRECEDENCE";
static gnutls_priority_t priority_cache;

int
plume_crypto_init(void)
{
  return motmot_net_gnutls_init(&priority_cache, priorities);
}

int
plume_crypto_deinit(void)
{
  return motmot_net_gnutls_deinit(priority_cache);
}

#endif /* USE_GNUTLS */
