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

void
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
