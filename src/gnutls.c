#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "crypto.h"
#include "gnutls.h"
#include "log.h"

// Our priorities set. Any 256-bit, secure cipher that is compatible with
// DTLS1.0 is fine, and should be secure for the foreseable future.
static const char *priorities =
  "SECURE256:-VERS-TLS-ALL:+VERS-DTLS1.0:%SERVER_PRECEDENCE";
static gnutls_priority_t priority_cache;

int trill_tls_can_read(struct trill_connection *conn);
int trill_tls_can_write(struct trill_connection *conn);
int trill_tls_handshake_retry(struct trill_connection *conn);

int
trill_crypto_init(void)
{
  if (gnutls_global_init()) {
    log_error("Error initializing gnutls");
    return 1;
  }

  if (gnutls_priority_init(&priority_cache, priorities, NULL)) {
    log_error("Error initializing priority cache");
    return 1;
  }

  return 0;
}

int
trill_crypto_deinit(void)
{
  gnutls_priority_deinit(priority_cache);
  gnutls_global_deinit();

  return 0;
}

int
trill_tls_init(struct trill_connection *conn)
{
  assert(conn != NULL);

  if (gnutls_certificate_allocate_credentials(&conn->tc_tls.tt_creds)) {
    log_error("Error allocating crypto credentials");
    return 1;
  }

  return 0;
}

int
trill_tls_free(struct trill_connection *conn)
{
  assert(conn != NULL);

  // Attempt to gracefully disconnect. Note that this could return an error code
  // (GNUTLS_E_AGAIN, or maybe a real error), but we don't care about that,
  // since we'll be dropping the underlying UDP transport in just a bit anyways.
  gnutls_bye(conn->tc_tls.tt_session, GNUTLS_SHUT_RDWR);

  gnutls_deinit(conn->tc_tls.tt_session);

  gnutls_certificate_free_credentials(conn->tc_tls.tt_creds);

  return 0;
}

int
trill_set_key(struct trill_connection *conn, const char *key_pem,
    size_t key_len, const char *cert_pem, size_t cert_len)
{
  gnutls_datum_t key, cert;

  assert(conn != NULL);
  assert(key_pem != NULL && key_len > 0);
  assert(cert_pem != NULL && cert_len > 0);

  // This is safe, since gnutls_certificate_set_x509_key_mem doesn't modify its
  // parameters
  key.data = (unsigned char *) key_pem;
  key.size = key_len;
  cert.data = (unsigned char *) cert_pem;
  cert.size = cert_len;

  return gnutls_certificate_set_x509_key_mem(conn->tc_tls.tt_creds, &cert, &key,
      GNUTLS_X509_FMT_PEM);
}

int
trill_set_ca(struct trill_connection *conn, const char *ca_pem, size_t ca_len)
{
  gnutls_datum_t ca;

  assert(conn != NULL);
  assert(ca_pem != NULL && ca_len > 0);

  ca.data = (unsigned char *) ca_pem;
  ca.size = ca_len;

  return gnutls_certificate_set_x509_trust_mem(conn->tc_tls.tt_creds, &ca,
      GNUTLS_X509_FMT_PEM);
}

int
trill_start_tls(struct trill_connection *conn)
{
  unsigned int flags;

  assert(conn != NULL);

  log_info("Starting DTLS!");

  if (conn->tc_state == TC_STATE_SERVER) {
    flags = GNUTLS_SERVER | GNUTLS_DATAGRAM | GNUTLS_NONBLOCK;
  } else if (conn->tc_state == TC_STATE_CLIENT) {
    flags = GNUTLS_CLIENT | GNUTLS_DATAGRAM | GNUTLS_NONBLOCK;
  } else {
    assert(0 && "Unexpected state when initializing crypto");
  }

  if (gnutls_init(&conn->tc_tls.tt_session, flags)) {
    log_error("Unable to initialize new session");
    return 1;
  }

  if (gnutls_priority_set(conn->tc_tls.tt_session, priority_cache)) {
    log_error("Unable to set priorities on new session");
    return 1;
  }

  gnutls_credentials_set(conn->tc_tls.tt_session, GNUTLS_CRD_CERTIFICATE,
      conn->tc_tls.tt_creds);

  gnutls_transport_set_ptr(conn->tc_tls.tt_session,
      (gnutls_transport_ptr_t) (ssize_t) conn->tc_sock_fd);

  // TODO: We should probably determine this in a better way
  gnutls_dtls_set_mtu(conn->tc_tls.tt_session, 1500);

  conn->tc_can_read_cb = trill_tls_can_read;
  conn->tc_can_write_cb = trill_tls_can_write;

  trill_want_timeout_callback(conn->tc_event_loop_data,
      trill_tls_handshake_retry, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);

  trill_tls_handshake(conn);

  return 0;
}

int
trill_tls_handshake_retry(struct trill_connection *conn)
{
  assert(conn != NULL);

  if (conn->tc_state != TC_STATE_ESTABLISHED) {
    trill_tls_handshake(conn);
  }

  return conn->tc_state != TC_STATE_ESTABLISHED;
}

int
trill_tls_handshake(struct trill_connection *conn)
{
  int ret;

  assert(conn != NULL);
  assert((conn->tc_state == TC_STATE_SERVER ||
      conn->tc_state == TC_STATE_CLIENT) &&
      "In a bad state during handshake");
  assert(conn->tc_tls.tt_creds != NULL);
  assert(conn->tc_tls.tt_session != NULL);

  do {
    log_info("Attempting handshake");
    ret = gnutls_handshake(conn->tc_tls.tt_session);
  } while (gnutls_error_is_fatal(ret));

  if (ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED) {
    // Do we need a write?
    if (gnutls_record_get_direction(conn->tc_tls.tt_session) == 1) {
      log_info("We want a write");
      trill_want_write_callback(conn->tc_event_loop_data);
    } else {
      log_info("We want a read");
    }
  } else if (ret == 0) {
    log_info("TLS is established!");
    conn->tc_state = TC_STATE_ESTABLISHED;
    if (conn->tc_connected_cb != NULL) {
      conn->tc_connected_cb(conn->tc_event_loop_data);
    }
  } else {
    log_error("Something went wrong with the TLS handshake");
  }

  return 0;
}

ssize_t
trill_tls_send(struct trill_connection *conn, const void *data, size_t len)
{
  ssize_t ret;
  assert(conn != NULL);
  assert(data != NULL);
  assert(len > 0);
  assert(conn->tc_state == TC_STATE_ESTABLISHED);

  ret = gnutls_record_send(conn->tc_tls.tt_session, data, len);

  // GnuTLS, like all helpful libraries, tries to translate errno errors into
  // other negative-numbered error codes. Naturally, we don't want that. Make
  // some kind of best-effort attempt to unmap the errors, so stdio-like clients
  // can consume them in a more standard format.
  switch (ret) {
    case GNUTLS_E_AGAIN:
      errno = EAGAIN;
      trill_want_write_callback(conn->tc_event_loop_data);
      return -1;
    case GNUTLS_E_INTERRUPTED:
      errno = EINTR;
      trill_want_write_callback(conn->tc_event_loop_data);
      return -1;
  }

  if (ret < 0) {
    ret = -1;
  }

  return ret;
}

int
trill_tls_can_read(struct trill_connection *conn)
{
  // TODO: How big should this be? This is bigger than the MTU, so it should be
  // "good enough," but the size is pretty arbitrary.
  static char buf[2048];
  static uint64_t seq;
  ssize_t len;

  switch (conn->tc_state) {
    case TC_STATE_SERVER:
    case TC_STATE_CLIENT:
      trill_tls_handshake(conn);
      break;
    case TC_STATE_ESTABLISHED:
      len = gnutls_record_recv_seq(conn->tc_tls.tt_session, buf, sizeof(buf),
          (unsigned char *)&seq);

      // TODO: sequence number is in big-endian. It'd probably be more useful to
      // everyone if it was converted into host endinanness, but be64toh is
      // Linux only.

      assert(conn->tc_recv_cb != NULL && "No callback set");
      conn->tc_recv_cb(conn->tc_event_loop_data, buf, len, &seq);
      break;
    default:
      assert(0 && "In an unexpected state in crypto read");
  }
  return 1;
}

int
trill_tls_can_write(struct trill_connection *conn)
{
  switch (conn->tc_state) {
    case TC_STATE_SERVER:
    case TC_STATE_CLIENT:
      trill_tls_handshake(conn);
      break;
    case TC_STATE_ESTABLISHED:
      gnutls_record_send(conn->tc_tls.tt_session, NULL, 0);
      break;
    default:
      assert(0 && "In an unexpected state in crypto write");
  }
  return 0;
}
