/**
 * session.h - State for a single session of Paxos (e.g., a single Motmot
 * chat session).
 */
#ifndef __PAXOS_TYPES_SESSION_H__
#define __PAXOS_TYPES_SESSION_H__

#include "paxos_msgpack.h"

#include "containers/list_factory.h"
#include "types/primitives.h"
#include "types/core.h"
#include "types/continuation.h"
#include "types/session_local.h"

/* Preparation state used by new proposers. */
struct paxos_prep {
  ballot_t pp_ballot;                 // ballot being prepared
  unsigned pp_acks;                   // number of prepare acks
  unsigned pp_redirects;              // number of prepare rejects
  struct paxos_instance *pp_istart;   // last contiguous instance at prep time
};

/* Sync state used by proposers during sync. */
struct paxos_sync {
  unsigned ps_total;      // number of acceptors syncing
  unsigned ps_acks;       // number of sync acks
  unsigned ps_skips;      // number of times we skipped starting a new sync
  paxid_t ps_last;        // the last contiguous learn across the system
};

/* Session state. */
struct paxos_session {
  pax_uuid_t *session_id;             // ID of the Paxos session
  void *client_data;                  // opaque client session object

  paxid_t self_id;                    // our own acceptor ID
  paxid_t req_id;                     // local incrementing request ID
  struct paxos_acceptor *proposer;    // the acceptor we think is the proposer
  ballot_t ballot;                    // identity of the current ballot

  paxid_t gen_high;                   // high water mark of ballots we've seen
  struct paxos_prep *prep;            // prepare state; NULL if not preparing

  paxid_t sync_id;                    // locally-unique sync ID
  paxid_t sync_prev;                  // sync point of the last sync
  struct paxos_sync *sync;            // sync state; NULL if not syncing

  unsigned live_count;                // number of acceptors we think are live
  acceptor_container alist;           // list of all Paxos participants
  acceptor_container adefer;          // list of deferred hello acks
  continuation_list clist;            // list of connectinuations

  instance_container ilist;           // list of all instances
  instance_container idefer;          // list of deferred instances
  request_container rcache;           // cached requests waiting for commit

  paxid_t ibase;                      // base value for instance numbers
  paxid_t ihole;                      // number of first uncommitted instance
  struct paxos_instance *istart;      // lower bound instance of first hole

  LIST_ENTRY(paxos_session) session_le; // session list entry
};

LIST_DECLARE(session, pax_uuid_t *);
struct paxos_session *session_new(void *, int);
void session_destroy(struct paxos_session *);

#endif /* __PAXOS_TYPES_SESSION_H__ */
