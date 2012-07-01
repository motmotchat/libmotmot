/**
 * rpc_io.h - Wire-level fiddling for the RPC system.
 */

#ifndef __RPC_IO_H__
#define __RPC_IO_H__

#include <glib.h>
#include <prpl.h>
#include <sslconn.h>

// TODO(carl): this should be actually chosen
#define MOTMOT_PORT 1234

void rpc_connect(struct pm_account *);
void rpc_close(struct pm_account *);

#endif // __RPC_IO_H__
