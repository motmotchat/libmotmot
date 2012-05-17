/**
 * rpc.c - RPC calls to a motmot server
 */

#include "msgpackutil.h"
#include "purplemot.h"
#include "rpc.h"
#include "sslconn.h"
#include "prpl.h"

int
rpc_login(struct motmot_conn *conn)
{
  struct yak yak;

  yak_init(&yak, 3);
  yak_pack(&yak, int, OP_AUTHENTICATE_USER);

  yak_cstring(&yak, purple_account_get_username(conn->account));
  yak_cstring(&yak, purple_account_get_password(conn->account));

  purple_ssl_write(conn->gsc, UNYAK(&yak));

  yak_destroy(&yak);

  return TRUE;
}

void
rpc_get_all_statuses(struct motmot_conn *conn)
{
  struct yak yak;

  yak_init(&yak, 1);
  yak_pack(&yak, int, OP_GET_ALL_STATUSES);

  purple_ssl_write(conn->gsc, UNYAK(&yak));

  yak_destroy(&yak);
}

void
rpc_register_friend(struct motmot_conn *conn, const char *name)
{
  struct yak yak;

  yak_init(&yak, 2);
  yak_pack(&yak, int, OP_REGISTER_FRIEND);

  yak_cstring(&yak, name);

  purple_ssl_write(conn->gsc, UNYAK(&yak));

  yak_destroy(&yak);
}

void
rpc_unregister_friend(struct motmot_conn *conn, const char *name)
{
  struct yak yak;

  yak_init(&yak, 2);
  yak_pack(&yak, int, OP_UNREGISTER_FRIEND);

  yak_cstring(&yak, name);

  purple_ssl_write(conn->gsc, UNYAK(&yak));

  yak_destroy(&yak);
}

void
rpc_get_status(struct motmot_conn *conn, const char *name)
{
  struct yak yak;

  yak_init(&yak, 2);
  yak_pack(&yak, int, OP_GET_USER_STATUS);

  yak_cstring(&yak, name);

  purple_ssl_write(conn->gsc, UNYAK(&yak));

  yak_destroy(&yak);
}

void
rpc_register_status(struct motmot_conn *conn, int status)
{
  struct yak yak;

  yak_init(&yak, 2);
  yak_pack(&yak, int, OP_REGISTER_STATUS);

  yak_pack(&yak, int, status);

  purple_ssl_write(conn->gsc, UNYAK(&yak));

  yak_destroy(&yak);
}

void
rpc_accept_friend(struct motmot_conn *conn, const char *name)
{
  struct yak yak;

  yak_init(&yak, 2);
  yak_pack(&yak, int, OP_ACCEPT_FRIEND);

  yak_cstring(&yak, name);

  purple_ssl_write(conn->gsc, UNYAK(&yak));

  yak_destroy(&yak);
}
