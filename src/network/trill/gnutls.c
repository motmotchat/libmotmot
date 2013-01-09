/**
 * gnutls.c - TLS backend for Trill using GnuTLS.
 */
#ifdef USE_GNUTLS

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gnutls/gnutls.h>
#include <gnutls/dtls.h>

#include "common/log.h"
#include "crypto/gnutls.h"
#include "trill/common.h"
#include "trill/crypto.h"

// Our priorities set. Any 256-bit, secure cipher that is compatible with
// DTLS1.0 is fine, and should be secure for the foreseable future.
static const char *priorities =
  "SECURE256:-VERS-TLS-ALL:+VERS-DTLS1.0:%SERVER_PRECEDENCE";
static gnutls_priority_t priority_cache;

static int trill_tls_verify(gnutls_session_t);


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

int
trill_crypto_init(void)
{
  return motmot_net_gnutls_init(&priority_cache, priorities);
}

int
trill_crypto_deinit(void)
{
  return motmot_net_gnutls_deinit(priority_cache);
}

int
trill_tls_init(struct trill_connection *conn)
{
  assert(conn != NULL);

  if (gnutls_certificate_allocate_credentials(&conn->tc_tls.mt_creds)) {
    log_error("Error allocating crypto credentials");
    return 1;
  }

  gnutls_certificate_set_verify_function(conn->tc_tls.mt_creds,
      trill_tls_verify);

  return 0;
}

int
trill_tls_free(struct trill_connection *conn)
{
  assert(conn != NULL);

  // Attempt to gracefully disconnect.  Note that this could return an error
  // code (GNUTLS_E_AGAIN, or maybe a real error), but we don't care about
  // that since we'll be dropping the underlying UDP transport in just a bit
  // anyways.
  gnutls_bye(conn->tc_tls.mt_session, GNUTLS_SHUT_RDWR);

  gnutls_deinit(conn->tc_tls.mt_session);
  gnutls_certificate_free_credentials(conn->tc_tls.mt_creds);

  return 0;
}

int
trill_set_key(struct trill_connection *conn, const char *key_path,
    const char *cert_path)
{
  assert(conn != NULL);
  assert(key_path != NULL);
  assert(cert_path != NULL);

  return gnutls_certificate_set_x509_key_file(conn->tc_tls.mt_creds, cert_path,
      key_path, GNUTLS_X509_FMT_PEM);
}

int
trill_set_ca(struct trill_connection *conn, const char *ca_path)
{
  assert(conn != NULL);
  assert(ca_path != NULL);

  return gnutls_certificate_set_x509_trust_file(conn->tc_tls.mt_creds, ca_path,
      GNUTLS_X509_FMT_PEM);
}


///////////////////////////////////////////////////////////////////////////////
//
//  Handshake protocol.
//

static enum motmot_gnutls_status trill_tls_handshake(struct trill_connection *);
static int trill_tls_handshake_retry(void *);
static int trill_tls_retry_read(void *);
static int trill_tls_retry_write(void *);

static int trill_tls_recv(void *);

int
trill_start_tls(struct trill_connection *conn)
{
  int r;
  unsigned int flags;

  assert(conn != NULL);

  log_info("Starting DTLS!");

  if (conn->tc_state == TC_STATE_SERVER) {
    flags = GNUTLS_SERVER | GNUTLS_DATAGRAM | GNUTLS_NONBLOCK;
  } else if (conn->tc_state == TC_STATE_CLIENT) {
    flags = GNUTLS_CLIENT | GNUTLS_DATAGRAM | GNUTLS_NONBLOCK;
  } else {
    assert(0 && "Unexpected state when starting TLS");
  }

  if ((r = motmot_net_gnutls_start(&conn->tc_tls, flags, conn->tc_fd,
    priority_cache, (void *)conn))) {
    return r;
  }

  // TODO: We should probably determine this in a better way.
  gnutls_dtls_set_mtu(conn->tc_tls.mt_session, 1500);

  // Start handshaking, restarting if the timeout is exceeded.
  motmot_event_want_timeout(trill_tls_handshake_retry, conn, conn->tc_data,
      GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);
  trill_tls_handshake_retry((void *)conn);

  return 0;
}

/**
 * trill_tls_handshake_retry - Restart TLS handshaking from the top.
 */
static int
trill_tls_handshake_retry(void *arg)
{
  struct trill_connection *conn;

  conn = (struct trill_connection *)arg;
  assert(conn != NULL);

  if (conn->tc_state == TC_STATE_ESTABLISHED) {
    return 0;
  }

  switch (trill_tls_handshake(conn)) {
    case MOTMOT_GNUTLS_SUCCESS:
      return 0;
    case MOTMOT_GNUTLS_FAILURE:
      return 1;
    case MOTMOT_GNUTLS_RETRY_READ:
      return trill_want_read(conn, trill_tls_retry_read);
    case MOTMOT_GNUTLS_RETRY_WRITE:
      return trill_want_write(conn, trill_tls_retry_write);
  }

  // This should never happen.
  return 0;
}

/**
 * trill_tls_handshake - Wrapper around motmot_net_gnutls_handshake that
 * just performs success/failure handling and returns the status code.
 *
 * Nothing is done on RETRY_READ or RETRY_WRITE.
 */
static enum motmot_gnutls_status
trill_tls_handshake(struct trill_connection *conn)
{
  int r;

  assert(conn != NULL);
  assert((conn->tc_state == TC_STATE_SERVER ||
      conn->tc_state == TC_STATE_CLIENT) &&
      "In a bad state during handshake");

  r = motmot_net_gnutls_handshake(&conn->tc_tls);

  switch (r) {
    case MOTMOT_GNUTLS_SUCCESS:
      // Update state and set a new, permanent can-read handler.
      conn->tc_state = TC_STATE_ESTABLISHED;
      trill_want_read(conn, trill_tls_recv);
      trill_connected(conn, TRILL_SUCCESS);
      break;
    case MOTMOT_GNUTLS_FAILURE:
      trill_connected(conn, TRILL_ETLS);
      break;
    case MOTMOT_GNUTLS_RETRY_READ:
    case MOTMOT_GNUTLS_RETRY_WRITE:
      break;
  }

  return r;
}

/**
 * trill_tls_retry_read - Retry a handshake read.
 */
static int
trill_tls_retry_read(void *arg)
{
  switch (trill_tls_handshake((struct trill_connection *)arg)) {
    case MOTMOT_GNUTLS_RETRY_READ:
      return 1;
    case MOTMOT_GNUTLS_RETRY_WRITE:
      trill_want_write((struct trill_connection *)arg, trill_tls_retry_write);
    case MOTMOT_GNUTLS_SUCCESS:
    case MOTMOT_GNUTLS_FAILURE:
      break;
  }

  return 0;
}

/**
 * trill_tls_retry_read - Retry a handshake write.
 */
static int
trill_tls_retry_write(void *arg)
{
  switch (trill_tls_handshake((struct trill_connection *)arg)) {
    case MOTMOT_GNUTLS_RETRY_WRITE:
      return 1;
    case MOTMOT_GNUTLS_RETRY_READ:
      trill_want_read((struct trill_connection *)arg, trill_tls_retry_read);
    case MOTMOT_GNUTLS_SUCCESS:
    case MOTMOT_GNUTLS_FAILURE:
      break;
  }

  return 0;
}

/**
 * trill_tls_verify - Verify the peer's TLS certificate chain.
 */
static int
trill_tls_verify(gnutls_session_t session)
{
  unsigned int errors;
  struct trill_connection *conn = gnutls_session_get_ptr(session);

  gnutls_certificate_verify_peers3(session, conn->tc_remote_user, &errors);

  return errors == 0;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Transport functions.
//

/**
 * trill_tls_resend - Re-send a message over TLS.
 */
static int
trill_tls_resend(void *arg)
{
  struct trill_connection *conn = (struct trill_connection *)arg;

  gnutls_record_send(conn->tc_tls.mt_session, NULL, 0);

  return 0;
}

/**
 * trill_tls_send - Send a message over TLS.
 */
ssize_t
trill_tls_send(struct trill_connection *conn, const void *data, size_t len)
{
  ssize_t ret;

  assert(conn != NULL);
  assert(data != NULL);
  assert(len > 0);
  assert(conn->tc_state == TC_STATE_ESTABLISHED);

  ret = gnutls_record_send(conn->tc_tls.mt_session, data, len);

  // GnuTLS, like all helpful libraries, tries to translate errno errors into
  // other negative-numbered error codes. Naturally, we don't want that. Make
  // some kind of best-effort attempt to unmap the errors, so stdio-like clients
  // can consume them in a more standard format.
  switch (ret) {
    case GNUTLS_E_AGAIN:
      errno = EAGAIN;
      trill_want_write(conn, trill_tls_resend);
      break;
    case GNUTLS_E_INTERRUPTED:
      errno = EINTR;
      trill_want_write(conn, trill_tls_resend);
      break;
  }

  return ret < 0 ? -1 : ret;
}

/**
 * trill_tls_recv - Receive data and pass it to the user.
 */
static int
trill_tls_recv(void *arg)
{
  struct trill_connection *conn;
  // TODO: How big should this be? This is bigger than the MTU, so it should be
  // "good enough," but the size is pretty arbitrary.
  static char buf[2048];
  static uint64_t seq;
  ssize_t len;

  conn = (struct trill_connection *)arg;

  assert(conn != NULL);
  assert(conn->tc_state == TC_STATE_ESTABLISHED);

  // TODO: Sequence number is in big-endian.  It'd probably be more useful to
  // everyone if it was converted into host endinanness, but be64toh is Linux
  // only.
  len = gnutls_record_recv_seq(conn->tc_tls.mt_session, buf, sizeof(buf),
      (unsigned char *)&seq);

  if (conn->tc_recv_cb != NULL) {
    conn->tc_recv_cb(conn->tc_data, buf, len, seq);
  }

  return 1;
}

#endif /* USE_GNUTLS */
