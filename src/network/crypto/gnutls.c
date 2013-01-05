/**
 * gnutls.c - TLS utility wrappers.
 */
#ifdef USE_GNUTLS

#include "common/log.h"
#include "crypto/gnutls.h"

int
motmot_net_gnutls_init(gnutls_priority_t *priority_cache,
    const char *priorities)
{
  if (gnutls_global_init()) {
    log_error("Error initializing GnuTLS");
    return -1;
  }

  if (gnutls_priority_init(priority_cache, priorities, NULL)) {
    log_error("Error initializing priority cache");
    return -1;
  }

  return 0;
}

int
motmot_net_gnutls_deinit(gnutls_priority_t priority_cache)
{
  gnutls_priority_deinit(priority_cache);
  gnutls_global_deinit();

  return 0;
}

#endif /* USE_GNUTLS */
