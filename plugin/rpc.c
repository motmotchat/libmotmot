/**
 * rpc.c - RPC calls to a motmot server
 */

#include "msgpackutil.h"
#include "purplemot.h"
#include "rpc.h"
#include "sslconn.h"
#include "prpl.h"

// Helper macros
#define rpc_op(y, op, nargs)                  \
  struct yak y;                               \
  yak_init(&y, 1 + (nargs));                  \
  yak_pack(&y, int, op);

#define rpc_op_end(y, conn)                   \
  purple_ssl_write(conn->gsc, UNYAK(&yak));   \
  yak_destroy(&y);


// TODO: This file needs a lot of error handling. In particular,
// purple_ssl_write returns the number of bytes written, which might at some
// point involve some connection buffer that ensures yaks get written

void
rpc_login(struct motmot_conn *conn)
{
  rpc_op(yak, OP_AUTHENTICATE_USER, 2);

  yak_cstring(&yak, purple_account_get_username(conn->account));
  yak_cstring(&yak, purple_account_get_password(conn->account));

  rpc_op_end(yak, conn);
}

void
rpc_get_all_statuses(struct motmot_conn *conn)
{
  rpc_op(yak, OP_GET_ALL_STATUSES, 0);
  rpc_op_end(yak, conn);
}

void
rpc_register_friend(struct motmot_conn *conn, const char *name)
{
  rpc_op(yak, OP_REGISTER_FRIEND, 1);
  yak_cstring(&yak, name);
  rpc_op_end(yak, conn);
}

void
rpc_unregister_friend(struct motmot_conn *conn, const char *name)
{
  rpc_op(yak, OP_UNREGISTER_FRIEND, 1);
  yak_cstring(&yak, name);
  rpc_op_end(yak, conn);
}

void
rpc_get_status(struct motmot_conn *conn, const char *name)
{
  rpc_op(yak, OP_GET_USER_STATUS, 1);
  yak_cstring(&yak, name);
  rpc_op_end(yak, conn);
}

void
rpc_register_status(struct motmot_conn *conn, int status)
{
  rpc_op(yak, OP_REGISTER_STATUS, 1);
  yak_pack(&yak, int, status);
  rpc_op_end(yak, conn);
}

void
rpc_accept_friend(struct motmot_conn *conn, const char *name)
{
  rpc_op(yak, OP_ACCEPT_FRIEND, 1);
  yak_cstring(&yak, name);
  rpc_op_end(yak, conn);
}
