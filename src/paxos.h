/**
 * paxos.h - Paxos interface.
 */
#ifndef __PAXOS_H__
#define __PAXOS_H__

#include <glib.h>

#include "motmot.h"
#include "types/primitives.h"
#include "types/core.h"
#include "types/decree.h"
#include "types/acceptor.h"
#include "types/continuation.h"
#include "types/connect.h"
#include "types/session.h"

/* Table of client learning callbacks. */
struct learn_table {
  learn_t chat;
  learn_t join;
  learn_t part;
};

/* Paxos protocol interface. */
int paxos_init(connect_t, struct learn_table *, enter_t, leave_t,
    const char *, size_t);
void *paxos_start(void *);
int paxos_end(void *data);

int paxos_register_connection(GIOChannel *);
int paxos_drop_connection(struct paxos_peer *);

int paxos_request(struct paxos_session *, dkind_t, const void *, size_t len);
int paxos_sync(void *);

/**
 *    Wire Protocol:
 *
 * Each message sent between two Paxos participants is a msgpack array of
 * either one or two elements.  The first, included in all messages, whether
 * in- or out-of-band, is a paxos_header.  The second is optional and
 * depends on the message opcode (which is found in the header):
 *
 * - OP_PREPARE: None.
 * - OP_PROMISE: A variable-length array of packed paxos_instance objects.
 * - OP_DECREE: The paxos_value of the decree.
 * - OP_ACCEPT: None.
 * - OP_COMMIT: The paxos_value of the commit.
 *
 * - OP_WELCOME: An array consisting of the starting instance number (which
 *   respects truncation), the alist, and the ilist of the proposer, used
 *   to initialize the newcomer.
 * - OP_HELLO: None.
 *
 * - OP_REQUEST: The paxos_request object.
 * - OP_RETRIEVE: A msgpack array containing the ID of the retriever and
 *   the paxos_value referencing the request.
 * - OP_RESEND: The paxos_request object being resent.
 *
 * - OP_REDIRECT: The header of the message that resulted in our redirecting.
 * - OP_REFUSE: The header of the message that resulted in our refusal, along
 *   with the request ID of the offending request.
 * - OP_REJECT: None.
 *
 * - OP_RETRY: None.
 * - OP_COMMIT: The paxos_value of the commit.
 *
 * - OP_SYNC: None.
 * - OP_LAST: The instance number of the acceptor's last contiguous learn.
 * - OP_TRUNCATE: The new starting point of the instance log.
 *
 * The message formats of the various Paxos structures can be found in
 * paxos_msgpack.c.
 */

#endif /* __PAXOS_H__ */
