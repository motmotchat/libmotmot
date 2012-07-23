/**
 * continuation.h - Continuation-like callbacks passed to the client to enable
 * post-processing after the establishment of new connections.
 */
#ifndef __PAXOS_TYPES_CONTINUATION_H__
#define __PAXOS_TYPES_CONTINUATION_H__

#include "motmot.h"

#include "containers/list_factory.h"
#include "types/primitives.h"
#include "types/core.h"
#include "types/session_local.h"

/* Continuation-style callbacks for connect_t calls. */
struct paxos_continuation {
  struct motmot_connect_cb pk_cb;     // Callback object
  pax_uuid_t *pk_session_id;          // session ID of the continuation
  paxid_t pk_paxid;                   // ID of the target acceptor
  union {
    paxid_t inum;                     // instance number for ack_reject
    struct paxos_request req;         // request value for ack_refuse
  } pk_data;
  LIST_ENTRY(paxos_continuation) pk_le;   // list entry
};

/* Continuation list. */
typedef LIST_HEAD(continuation_list, paxos_continuation) continuation_list;
void continuation_list_destroy(continuation_list *);

/* Creation and destruction. */
struct paxos_continuation *
  continuation_new(motmot_connect_continuation_t, paxid_t);
void continuation_destroy(struct paxos_continuation *);

#endif /* __PAXOS_TYPES_CONTINUATION_H__ */
