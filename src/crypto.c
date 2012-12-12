#include <assert.h>
#include <stdlib.h>

#include "crypto.h"
#include "log.h"

// Our priorities set. Any 256-bit, secure cipher that is compatible with
// DTLS1.0 is fine, and should be secure for the foreseable future.
static const char *priorities =
  "SECURE256:-VERS-TLS-ALL:+VERS-DTLS1.0:%SERVER_PRECEDENCE";
static gnutls_priority_t priority_cache;

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

void
trill_crypto_deinit(void)
{
  gnutls_priority_deinit(priority_cache);
  gnutls_global_deinit();
}

struct trill_crypto_identity *
trill_crypto_identity_new(const char *cafile, const char *crlfile,
    const char *certfile, const char *keyfile)
{
  struct trill_crypto_identity *id;

  assert(certfile != NULL && "You must provide a certificate");
  assert(keyfile != NULL && "You must provide a private key");

  id = malloc(sizeof(*id));

  if (gnutls_certificate_allocate_credentials(&id->tci_creds)) {
    log_error("Error allocating crypto credentials");
    free(id);
    return NULL;
  }

  if (cafile != NULL) {
    if (gnutls_certificate_set_x509_trust_file(id->tci_creds, cafile,
          GNUTLS_X509_FMT_PEM) < 0) {
      log_error("Error setting x509 CA file");
      trill_crypto_identity_free(id);
      return NULL;
    }
  }

  if (crlfile != NULL) {
    if (gnutls_certificate_set_x509_crl_file(id->tci_creds, crlfile,
          GNUTLS_X509_FMT_PEM) < 0) {
      log_error("Error setting x509 CRL file");
      trill_crypto_identity_free(id);
      return NULL;
    }
  }

  if (gnutls_certificate_set_x509_key_file(id->tci_creds, certfile, keyfile,
        GNUTLS_X509_FMT_PEM)) {
    log_error("Error setting x509 keys");
    trill_crypto_identity_free(id);
    return NULL;
  }

  return id;
}

void
trill_crypto_identity_free(struct trill_crypto_identity *id)
{
  assert(id != NULL && "Attempting to free a null identity");

  gnutls_certificate_free_credentials(id->tci_creds);
  free(id->tci_creds);
}

int
trill_crypto_session_init(struct trill_connection *conn)
{
  struct trill_crypto_session *session;
  unsigned int flags;

  log_warn("Starting DTLS!");

  assert(conn != NULL && "Attempting to initalize crypto for null");

  session = conn->tc_crypto = calloc(1, sizeof(*conn->tc_crypto));
  if (session == NULL) {
    return 1;
  }
  conn->tc_crypto->tcs_conn = conn;

  if (conn->tc_state == TC_STATE_HANDSHAKE_SERVER ||
      conn->tc_state == TC_STATE_PRESHAKE_SERVER) {
    flags = GNUTLS_SERVER | GNUTLS_DATAGRAM | GNUTLS_NONBLOCK;
  } else if (conn->tc_state == TC_STATE_HANDSHAKE_CLIENT) {
    flags = GNUTLS_CLIENT | GNUTLS_DATAGRAM | GNUTLS_NONBLOCK;
  } else {
    assert(0 && "Unexpected state when initializing crypto");
  }

  if (gnutls_init(&session->tcs_session, flags)) {
    log_error("Unable to initialize new session");
    free(session);
    conn->tc_crypto = NULL;
    return 1;
  }

  if (gnutls_priority_set(session->tcs_session, priority_cache)) {
    log_error("Unable to set priorities on new session");
    free(session);
    conn->tc_crypto = NULL;
    return 1;
  }

  gnutls_transport_set_ptr(session->tcs_session,
      (gnutls_transport_ptr_t) (ssize_t) conn->tc_sock_fd);

  // TODO: We should probably determine this in a better way
  gnutls_dtls_set_mtu(session->tcs_session, 1500);

  gnutls_handshake_set_timeout(session->tcs_session,
      GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);

  //gnutls_dtls_get_timeout(session->tcs_session);

  conn->tc_can_read_cb = trill_crypto_can_read;
  conn->tc_can_read_cb = trill_crypto_can_write;

  trill_crypto_handshake(session); // TODO: check for errors

  return 0;
}

int
trill_crypto_handshake(struct trill_crypto_session *session)
{
  int ret;

  do {
    log_warn("Attempting handshake");
    ret = gnutls_handshake(session->tcs_session);
  } while (gnutls_error_is_fatal(ret));

  if (ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED) {
    // Do we need a write?
    if (gnutls_record_get_direction(session->tcs_session) == 1) {
      log_info("We want a write");
      trill_net_want_write_cb(session->tcs_conn);
    }
  } else if (ret == 0) {
    log_info("TLS is established!");
    session->tcs_conn->tc_state = TC_STATE_ENCRYPTED;
  } else {
    log_error("Something went wrong with the TLS handshake");
  }

  return 0;
}

int
trill_crypto_can_read(struct trill_connection *conn)
{
  log_info("Can read");
  char buf[1024];
  switch (conn->tc_state) {
    case TC_STATE_HANDSHAKE_SERVER:
    case TC_STATE_PRESHAKE_SERVER:
    case TC_STATE_HANDSHAKE_CLIENT:
      trill_crypto_handshake(conn->tc_crypto);
      break;
    case TC_STATE_ENCRYPTED:
      gnutls_record_recv(conn->tc_crypto->tcs_session, buf, sizeof(buf));
      log_info("MSG: %s\n", buf);
      break;
    default:
      assert(0 && "In an unexpected state in crypto read");
  }
  return 1;
}

int
trill_crypto_can_write(struct trill_connection *conn)
{
  log_info("Can write");
  switch (conn->tc_state) {
    case TC_STATE_HANDSHAKE_SERVER:
    case TC_STATE_PRESHAKE_SERVER:
    case TC_STATE_HANDSHAKE_CLIENT:
      trill_crypto_handshake(conn->tc_crypto);
      break;
    case TC_STATE_ENCRYPTED:
      // Resume the write
      gnutls_record_send(conn->tc_crypto->tcs_session, NULL, 0);
      break;
    default:
      assert(0 && "In an unexpected state in crypto write");
  }
  return 0;
}
