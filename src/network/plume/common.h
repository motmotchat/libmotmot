/**
 * common.h - Plume client internal interface.
 */
#ifndef __PLUME_COMMON_H__
#define __PLUME_COMMON_H__

struct plume_client {
  int pc_sock_fd;
  char *pc_cert_path;

  plume_callback_t pc_connect;
  plume_recv_callback_t pc_recv;

  void *pc_data;
};

#endif /* __PLUME_COMMON_H__ */
