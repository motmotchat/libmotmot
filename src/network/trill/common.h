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

/* Trill peer connection type. */
struct trill_connection {
  int tc_fd;
  enum trill_state tc_state;
  uint16_t tc_port;

  void *tc_data;

  uint32_t tc_server_priority[2];

  char *tc_remote_user;

  trill_status_callback_t tc_connected_cb;
  trill_recv_callback_t tc_recv_cb;

  struct motmot_net_tls tc_tls;
};

void trill_connected(struct trill_connection *, int);

int trill_want_read(struct trill_connection *, motmot_event_callback_t);
int trill_want_write(struct trill_connection *, motmot_event_callback_t);

#endif // __TRILL_COMMON_H__
