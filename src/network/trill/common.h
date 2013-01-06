#ifndef __TRILL_COMMON_H__
#define __TRILL_COMMON_H__

#include <stdint.h>

#include "trill/trill.h"
#include "crypto/crypto.h"
#include "event/callbacks.h"

struct trill_connection;

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

/* Motmot event layer wrappers. */
typedef int (*trill_callback_t)(struct trill_connection *);
int trill_want_read(struct trill_connection *);
int trill_want_write(struct trill_connection *);
int trill_can_read(void *);
int trill_can_write(void *);

/* Trill peer connection type. */
struct trill_connection {
  int tc_sock_fd;
  enum trill_state tc_state;
  uint16_t tc_port;

  void *tc_data;

  uint32_t tc_server_priority[2];

  char *tc_remote_user;

  trill_callback_t tc_can_read_cb;
  trill_callback_t tc_can_write_cb;

  trill_connected_callback_t tc_connected_cb;
  trill_recv_callback_t tc_recv_cb;

  struct motmot_net_tls tc_tls;
};

#endif // __TRILL_COMMON_H__
