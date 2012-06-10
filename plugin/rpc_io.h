/**
 * rpc_io.h - Wire-level fiddling for the RPC system.
 */

#ifndef __RPC_IO_H__
#define __RPC_IO_H__

#include <glib.h>
#include <prpl.h>
#include <sslconn.h>

void rpc_read(void *, PurpleSslConnection *, PurpleInputCondition);

#endif // __RPC_IO_H__
