/**
 * common.h - Plume client internal interface.
 */
#ifndef __PLUME_COMMON_H__
#define __PLUME_COMMON_H__

#include <ares.h>
#include <stdint.h>

#include "event/event.h"
#include "plume/plume.h"
#include "crypto/crypto.h"

enum plume_state {
  PLUME_STATE_INIT,
  PLUME_STATE_DNS_SRV,
  PLUME_STATE_DNS_HOST,
  PLUME_STATE_CONNECTING,
  PLUME_STATE_TLS,
  PLUME_STATE_CONNECTED
};

struct plume_client {
  int pc_fd;            // socket underlying the connection
  char *pc_host;        // hostname of the Plume server
  char *pc_ip;          // IP of the Plume server
  uint16_t pc_port;     // port of the Plume server

  char *pc_cert;        // client's identity cert
  size_t pc_cert_size;  // size in bytes of the identity cert
  char *pc_handle;      // client's username; extracted from cert
  void *pc_data;        // opaque user data associated to the connection

  enum plume_state pc_state;            // connect protocol state
  plume_connect_callback_t pc_connect;  // called after connect completes

  ares_channel pc_ares_chan_srv;  // c-ares SRV lookup channel
  ares_channel pc_ares_chan_host; // c-ares host lookup channel
  struct motmot_net_tls pc_tls;   // crypto-backend specific TLS data
};

int plume_want_read(struct plume_client *, motmot_event_callback_t cb);
int plume_want_write(struct plume_client *, motmot_event_callback_t cb);

#endif /* __PLUME_COMMON_H__ */
