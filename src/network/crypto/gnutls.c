/**
 * gnutls.c - TLS utility routines.
 */
#ifdef USE_GNUTLS

#include <assert.h>
#include <errno.h>

#include "common/log.h"
#include "crypto/gnutls.h"
#include "event/event.h"
#include "event/callbacks.h"


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

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
  tls->mt_flags = flags;

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


///////////////////////////////////////////////////////////////////////////////
//
//  Handshake protocol.
//

enum motmot_gnutls_status
motmot_net_gnutls_handshake(struct motmot_net_tls *tls)
{
  int r;

  assert(tls->mt_creds != NULL);
  assert(tls->mt_session != NULL);

  do {
    log_debug("Attempting TLS handshake");
    r = gnutls_handshake(tls->mt_session);
  } while (gnutls_error_is_fatal(r));

  if (r == GNUTLS_E_AGAIN || r == GNUTLS_E_INTERRUPTED) {
    r = gnutls_record_get_direction(tls->mt_session);
    if (r == 0) {
      return MOTMOT_GNUTLS_RETRY_READ;
    } else if (r == 1) {
      return MOTMOT_GNUTLS_RETRY_WRITE;
    }
  } else if (r == 0) {
    log_info("TLS established");
    return MOTMOT_GNUTLS_SUCCESS;
  } else {
    log_error("Something went wrong with the TLS handshake");
  }

  return MOTMOT_GNUTLS_FAILURE;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Transport functions.
//

/**
 * motmot_net_gnutls_resend - Re-send a message over TLS.
 */
static int
motmot_net_gnutls_resend(void *arg)
{
  struct motmot_net_tls *tls = (struct motmot_net_tls *)arg;

  gnutls_record_send(tls->mt_session, NULL, 0);

  return 0;
}

/**
 * motmot_net_gnutls_send - Send a message over TLS.
 */
ssize_t
motmot_net_gnutls_send(struct motmot_net_tls *tls, void *data, const void *buf,
    size_t len)
{
  ssize_t ret;
  int fd;
  enum motmot_fdtype fdtype;

  assert(tls != NULL);
  assert(buf != NULL);
  assert(len > 0);

  ret = gnutls_record_send(tls->mt_session, buf, len);

  fd = (int)(ssize_t)gnutls_session_get_ptr(tls->mt_session);
  fdtype = tls->mt_flags & GNUTLS_DATAGRAM ? MOTMOT_EVENT_UDP : MOTMOT_EVENT_TCP;

  switch (ret) {
    case GNUTLS_E_AGAIN:
      errno = EAGAIN;
      motmot_event_want_write(fd, fdtype, data, motmot_net_gnutls_resend, tls);
      break;
    case GNUTLS_E_INTERRUPTED:
      errno = EINTR;
      motmot_event_want_write(fd, fdtype, data, motmot_net_gnutls_resend, tls);
      break;
  }

  return ret >= 0 ? ret : -1;
}

#endif /* USE_GNUTLS */
