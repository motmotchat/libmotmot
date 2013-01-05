/**
 * common.h - Plume client internal interface.
 */
#ifndef __PLUME_COMMON_H__
#define __PLUME_COMMON_H__

#include "plume/plume.h"
#include "crypto/crypto.h"

struct plume_client {
  int pc_fd;            // socket underlying the connection
  char *pc_cert;        // client's identity cert
  size_t pc_cert_size;  // size in bytes of the identity cert
  char *pc_handle;      // client's username; extracted from cert
  void *pc_data;        // opaque user data associated to the connection

  plume_callback_t pc_connect;    // callback for use after connect completes
  plume_recv_callback_t pc_recv;  // callback for use upon data receipt

  struct motmot_net_tls pc_tls;   // crypto-backend specific TLS data
};

#endif /* __PLUME_COMMON_H__ */
