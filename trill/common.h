#ifndef __TRILL_COMMON_H__
#define __TRILL_COMMON_H__

#include <stdint.h>

#include "trill.h"

#if defined(TRILL_USE_GNUTLS)
#include "gnutls.h"
#elif defined(TRILL_USE_OPENSSL)
#error "OpenSSL not implemented yet!"
#elif defined(TRILL_USE_NSS)
#error "NSS not implemented yet!"
#else
#error "Unknown cryptographic library"
#endif

/**
 * Connection state diagram:
 *
 *          INIT (new)
 *              |
 *              | (connect)
 *              v
 *           PROBING  -- (recv probe, we win) -->  SERVER
 *              |                                    |
 *              | (recv probe, we lose,              | (DTLS handshake)
 *              |  acknowledge bit set)              |
 *              v                                    v
 *           CLIENT  -- (DTLS handshake) -----> ESTABLISHED
 *
 */
enum trill_state {
  TC_STATE_INIT,
  TC_STATE_PROBING,
  TC_STATE_SERVER,
  TC_STATE_CLIENT,
  TC_STATE_ESTABLISHED
};

struct trill_connection {
  int tc_sock_fd;
  enum trill_state tc_state;
  uint16_t tc_port;

  void *tc_event_loop_data;

  uint32_t tc_server_priority[2];

  char *tc_remote_user;

  trill_callback_t tc_can_read_cb;
  trill_callback_t tc_can_write_cb;

  trill_connected_callback_t tc_connected_cb;
  trill_recv_callback_t tc_recv_cb;

  struct trill_tls tc_tls;
};

trill_want_write_callback_t trill_want_write_callback;
trill_want_timeout_callback_t trill_want_timeout_callback;

#endif // __TRILL_COMMON_H__
