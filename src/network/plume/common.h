/**
 * common.h - Plume client internal interface.
 */
#ifndef __PLUME_COMMON_H__
#define __PLUME_COMMON_H__

#include <ares.h>
#include <stdint.h>

#include "plume/plume.h"
#include "crypto/crypto.h"

struct plume_client {
  int pc_fd;            // socket underlying the connection
  char *pc_host;        // hostname of the Plume server
  char *pc_ip;          // IP of the Plume server
  uint16_t pc_port;     // port of the Plume server
  int pc_started;       // whether connect has been attempted

  char *pc_cert;        // client's identity cert
  size_t pc_cert_size;  // size in bytes of the identity cert
  char *pc_handle;      // client's username; extracted from cert
  void *pc_data;        // opaque user data associated to the connection

  plume_connect_callback_t pc_connect;  // called after connect completes
  plume_recv_callback_t pc_recv;        // called upon data receipt

  ares_channel pc_ares_chan;      // c-ares DNS query channel
  struct motmot_net_tls pc_tls;   // crypto-backend specific TLS data
};

#endif /* __PLUME_COMMON_H__ */
