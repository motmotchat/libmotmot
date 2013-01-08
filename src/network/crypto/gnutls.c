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

int
motmot_net_gnutls_start(struct motmot_net_tls *tls, unsigned flags,
    int fd, gnutls_priority_t priority_cache, void *data)
{
  if (gnutls_init(&tls->mt_session, flags)) {
    log_error("Unable to initialize new session");
    return -1;
  }

  gnutls_session_set_ptr(tls->mt_session, data);

  if (gnutls_priority_set(tls->mt_session, priority_cache)) {
    log_error("Unable to set priorities on new session");
    return -1;
  }

  gnutls_credentials_set(tls->mt_session, GNUTLS_CRD_CERTIFICATE,
      tls->mt_creds);

  gnutls_transport_set_ptr(tls->mt_session,
      (gnutls_transport_ptr_t)(ssize_t)fd);

  return 0;
}

#endif /* USE_GNUTLS */
