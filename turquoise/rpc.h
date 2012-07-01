/**
 * rpc.h - RPC calls to a motmot server
 */

#ifndef __RPC_H__
#define __RPC_H__

#include <msgpack.h>

#include "purplemot.h"

typedef enum rpc_opcode {
  OP_AUTHENTICATE_USER = 1,
  OP_REGISTER_FRIEND = 2,
  OP_UNREGISTER_FRIEND = 3,
  OP_GET_FRIEND_IP = 4,
  OP_REGISTER_STATUS = 5,
  OP_ACCEPT_FRIEND = 6,
  OP_GET_ALL_STATUSES = 7,
  OP_GET_USER_STATUS = 8,

  OP_PUSH_CLIENT_STATUS = 20,
  OP_PUSH_FRIEND_ACCEPT = 21,
  OP_PUSH_FRIEND_REQUEST = 22,

  OP_AUTHENTICATE_SERVER = 30,
  OP_SERVER_SEND_FRIEND = 31,
  OP_SERVER_SEND_UNFRIEND = 32,
  OP_SERVER_SEND_STATUS_CHANGED = 33,
  OP_SERVER_SEND_ACCEPT = 34,
  OP_SERVER_GET_STATUS = 35,

  OP_SUCCESS = 60,
  OP_AUTHENTICATED = 61,
  OP_AUTH_FAILED = 62,
  OP_ACCESS_DENIED = 63,
  OP_ALL_STATUS_RESPONSE = 65,
  OP_SERVER_GET_STATUS_RESP = 66,
  OP_GET_STATUS_RESP = 68,

  OP_FRIEND_SERVER_DOWN = 91
} rpcop_t;

int rpc_dispatch(struct pm_account *, const msgpack_object *);

void rpc_login(struct pm_account *);
void rpc_get_all_statuses(struct pm_account *);
void rpc_register_friend(struct pm_account *, const char *);
void rpc_unregister_friend(struct pm_account *, const char *);
void rpc_get_status(struct pm_account *, const char *);
void rpc_register_status(struct pm_account *, int);
void rpc_accept_friend(struct pm_account *, const char *);

#endif // __RPC_H__
