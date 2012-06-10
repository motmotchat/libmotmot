/**
 * rpc_io.c - Wire-level fiddling for the RPC system.
 */

#include <sslconn.h>
#include <errno.h>

#include "rpc.h"
#include "rpc_io.h"

// XXX: these should be coming from somewhere else
#define BUFSIZE 512
#define _

static void rpc_connect_success(void *, PurpleSslConnection *,
    PurpleInputCondition);
static void rpc_connect_error(PurpleSslConnection *, PurpleSslErrorType,
    void *);
static void rpc_read(void *, PurpleSslConnection *, PurpleInputCondition);

void
rpc_connect(struct pm_account *account)
{
  account->gsc = purple_ssl_connect(account->pa, account->server_host,
      MOTMOT_PORT, rpc_connect_success, rpc_connect_error, account);
}

void
rpc_close(struct pm_account *account)
{
  purple_ssl_close(account->gsc);
}

static void
rpc_connect_success(void *data, PurpleSslConnection *gsc,
    PurpleInputCondition cond)
{
  struct pm_account *account;
  PurpleStatus *status;

  account = data;

  rpc_login(account);
  // TODO(carl): This should probably get pushed at us, not requested.
  rpc_get_all_statuses(account);

  purple_ssl_input_add(gsc, rpc_read, account);

  // Publish our current status.
  status = purple_account_get_active_status(account->pa);
  // TODO(carl): make this actually work
  //motmot_report_status(purple_status_get_id(status), account);
}

static void
rpc_connect_error(PurpleSslConnection *gsc, PurpleSslErrorType error,
    void *data)
{
  struct pm_account *account = data;

  account->gsc = NULL;

  purple_connection_ssl_error(account->pa->gc, error);
}

static void
rpc_read(void *data, PurpleSslConnection *conn, PurpleInputCondition cond)
{
  struct pm_account *account;
  msgpack_unpacked result;
  size_t bytes_read;

  account = (struct pm_account *)data;

  msgpack_unpacked_init(&result);

  // Read for as long as we can.
  for (;;) {
    msgpack_unpacker_reserve_buffer(&account->unpacker, BUFSIZE);

    bytes_read = purple_ssl_read(conn,
        msgpack_unpacker_buffer(&account->unpacker), BUFSIZE);

    if (bytes_read < 0) {
      if (errno != EAGAIN && errno != EINTR) {
        char *errmsg = g_strdup_printf(_("Lost connection with server: %s"),
            g_strerror(errno));
        purple_connection_error_reason(account->pa->gc,
            PURPLE_CONNECTION_ERROR_NETWORK_ERROR, errmsg);
        g_free(errmsg);
      }
      break;
    }

    msgpack_unpacker_buffer_consumed(&account->unpacker, bytes_read);

    while (msgpack_unpacker_next(&account->unpacker, &result)) {
      // TODO: check return values
      rpc_dispatch(account, &result.data);
    }
  }

  msgpack_unpacked_destroy(&result);
}
