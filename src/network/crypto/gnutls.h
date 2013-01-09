/**
 * gnutls.h - GnuTLS helper datatypes and routines.
 */
#ifndef __MOTMOT_CRYPTO_GNUTLS_H__
#define __MOTMOT_CRYPTO_GNUTLS_H__

#include <gnutls/gnutls.h>

struct motmot_net_tls {
  gnutls_certificate_credentials_t mt_creds;
  gnutls_session_t mt_session;
  unsigned mt_flags;
};

enum motmot_gnutls_status {
  MOTMOT_GNUTLS_SUCCESS = 0,
  MOTMOT_GNUTLS_FAILURE,
  MOTMOT_GNUTLS_RETRY_READ,
  MOTMOT_GNUTLS_RETRY_WRITE,
};

/**
 * motmot_net_gnutls_init - Initialize GnuTLS along with a priority set.
 *
 * @param priority_cache  A pointer to the gnutls_priority_t.
 * @param priorities      String specifying the protocol priorities.
 * @return                0 on success, nonzero on failure.
 */
int motmot_net_gnutls_init(gnutls_priority_t *priority_cache,
    const char *priorities);

/**
 * motmot_net_gnutls_init - Deinitialize GnuTLS.
 *
 * @param priority_cache  A pointer to the gnutls_priority_t.
 * @return                0 on success, nonzero on failure.
 */
int motmot_net_gnutls_deinit(gnutls_priority_t priority_cache);

/**
 * motmot_net_gnutls_start - Start a TLS session over a nonblocking socket.
 *
 * @param tls             The motmot_net_tls object.
 * @param flags           Flags specifying the session.
 * @param fd              The fd over which to establish TLS.
 * @param priority_cache  The protocol priorities set.
 * @param data            Data to associate with the session.
 * @return                0 on success, nonzero on failure.
 */
int motmot_net_gnutls_start(struct motmot_net_tls *tls, unsigned flags, int fd,
    gnutls_priority_t priority_cache, void *data);

/**
 * motmot_net_gnutls_handshake - Attempt to perform a TLS handshake.
 *
 * @param tls       The motmot_net_tls object.
 * @return          A motmot_gnutls_status code.
 */
enum motmot_gnutls_status motmot_net_gnutls_handshake(
    struct motmot_net_tls *tls);

/**
 * motmot_net_gnutls_send - Send data to a remote user over TLS.  This is
 * nonblocking and makes use of the Motmot event layer.
 *
 * @param tls       The motmot_net_tls object.
 * @param data      Application data for event loop callbacks.
 * @param buf       The message to send.
 * @param len       Length of the data.
 * @return          The number of bytes sent, or -1 on error.  We unmap the
 *                  GnuTLS error codes so stdio-like clients can consume them
 *                  in a more standard format.
 */
ssize_t motmot_net_gnutls_send(struct motmot_net_tls *tls, void *data,
    const void *buf, size_t len);

#endif /* __MOTMOT_CRYPTO_GNUTLS_H__ */
