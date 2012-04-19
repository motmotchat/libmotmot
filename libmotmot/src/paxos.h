/**
 * paxos.h - Paxos interface
 */
#ifndef __PAXOS_H__
#define __PAXOS_H__

#include "list.h"
#include "motmot.h"
#include "paxos_io.h"

#include <stdint.h>
#include <string.h>
#include <msgpack.h>
#include <glib.h>

/* Paxos ID type. */
typedef uint32_t  paxid_t;

/* Totally ordered ID of a ballot. */
typedef struct paxid_pair {
  paxid_t id;           // ID of participant
  paxid_t gen;          // generation number of some sort
} ppair_t;

int ppair_compare(ppair_t, ppair_t);

/* Alias ballots as (proposer ID, ballot number). */
typedef ppair_t ballot_t;
int ballot_compare(ballot_t, ballot_t);

/* Paxos message types. */
typedef enum paxos_opcode {
  OP_PREPARE = 0,       // declare new proposership (NextBallot)
  OP_PROMISE,           // promise to ignore earlier proposers (LastVote)
  OP_DECREE,            // propose a decree (BeginBallot)
  OP_ACCEPT,            // accept a decree (Voted)
  OP_COMMIT,            // commit a decree (Success)
  OP_REQUEST,           // request a decree from the proposer
  OP_REDIRECT,          // suggests the true identity of the proposer
  OP_WELCOME,           // says hello to a new participant
  OP_SYNC,              // sync up ilists in preparation for a truncate
  OP_TRUNCATE           // order acceptors to truncate their ilists
} paxop_t;

/* Paxos message header that is included with any message. */
struct paxos_header {
  ballot_t ph_ballot;   // ballot ID
  paxop_t ph_opcode;    // protocol opcode
  paxid_t ph_inum;      // Multi-Paxos instance number
  /**
   * The ph_inum field means different things for the different ops:
   *
   * - OP_PREPARE: The lowest instance for which the preparer has not committed
   *   a value.  Any acceptor who accepts the prepare will return to us the
   *   most recent value they accepted for each Paxos instance they participated
   *   in, starting with ph_inum.
   *
   * - OP_PROMISE: The index of the first vote returned (as specified in
   *   the prepare message).
   *
   * - OP_DECREE, OP_ACCEPT, OP_COMMIT: The instance number of the decree.
   *
   * - OP_REQUEST: The paxid of the acceptor who we think is the proposer who
   *   will send our request.  This allows us to send a redirect appropriately.
   *   This overload is a little gross but requests are out-of-protocol anyway.
   *
   * - OP_REDIRECT: Not used.
   *
   * - OP_WELCOME: Sends the new acceptor's assigned paxid (which is, in fact,
   *   the instance number of its JOIN).
   *
   * - OP_SYNC: Sends the proposer-use-only ID of the sync, which is echoed.
   *
   * We start counting instances at 1 and use 0 as a sentinel value.
   */
};

/**
 * We describe the particular message formats for each Paxos message type:
 *
 * - OP_PREPARE: Nothing needed but the header.
 * - OP_PROMISE: We use the following array directly from the msgpack
 *   buffer:
 *
 *   struct paxos_promise {
 *     struct paoxs_hdr hdr   // We use only the ballot.
 *     struct paxos_value val;
 *   } votes[];
 *
 * - OP_DECREE: Decrees are headers plus values.
 * - OP_ACCEPT: Acceptances are headers.
 * - OP_COMMIT: Commits are just headers.
 * - OP_REQUEST: Requests are headers plus values.
 * - OP_REDIRECT: Redirects are two headers: the first one is the one with the
 *   corrected ballot, and the second is the original header.
 */

/* Kinds of decrees. */
typedef enum decree_kind {
  DEC_NULL = 0,         // null value
  DEC_CHAT,             // chat message
  DEC_RENEW,            // proposer lease renewal
  DEC_JOIN,             // add an acceptor
  DEC_PART,             // remove an acceptor
} dkind_t;

int is_request(dkind_t dkind);

/* Alias request ID's as (from ID, local request number). */
typedef ppair_t reqid_t;
int compare_reqid(reqid_t, reqid_t);

/* Decree value type. */
struct paxos_value {
  dkind_t pv_dkind;   // decree kind
  reqid_t pv_reqid;   // totally ordered request ID
  /**
   * In order to reduce network traffic, requesters broadcast any requests
   * with additional data to all acceptors, associating with each a session-
   * unique ID (the combination of the requester's acceptor ID with an
   * incrementing requester-local request number).  Any data they pass along
   * is queued up by the acceptors.  The proposer then makes decrees and
   * orders commits with reference to the request's unique ID.
   */
};

/* A Paxos protocol participant. */
struct paxos_acceptor {
  paxid_t pa_paxid;                   // agent's ID; also the instance number of
                                      //   the agent's JOIN decree
  struct paxos_peer *pa_peer;         // agent's connection information; NULL if
                                      //   we think it's dead
  LIST_ENTRY(paxos_acceptor) pa_le;   // sorted linked list of all participants
};
LIST_HEAD(acceptor_list, paxos_acceptor);

/* Representation of a Paxos instance. */
struct paxos_instance {
  struct paxos_header pi_hdr;         // Paxos header identifying the instance
  unsigned pi_votes;                  // number of accepts -OR- 0 if committed
  LIST_ENTRY(paxos_instance) pi_le;   // sorted linked list of instances
  struct paxos_value pi_val;          // value of the decree
};
LIST_HEAD(instance_list, paxos_instance);

/* Request containing (usually chat) data, pending proposer commit. */
struct paxos_request {
  struct paxos_value pr_val;          // request ID and kind
  size_t pr_size;                     // size of data
  void *pr_data;                      // data pointer dependent on kind
  LIST_ENTRY(paxos_request) pr_le;    // sorted linked list of requests
};
LIST_HEAD(request_list, paxos_request);

/* List helpers. */
struct paxos_acceptor *acceptor_find(struct acceptor_list *, paxid_t);
struct paxos_acceptor *acceptor_insert(struct acceptor_list *,
    struct paxos_acceptor *);
struct paxos_instance *instance_find(struct instance_list *, paxid_t);
struct paxos_instance *instance_insert(struct instance_list *,
    struct paxos_instance *);
struct paxos_request *request_find(struct request_list *, reqid_t);
struct paxos_request *request_insert(struct request_list *,
    struct paxos_request *);

/* Preparation state used by new proposers. */
struct paxos_prep {
  unsigned pp_nacks;                  // number of prepare acks
  paxid_t pp_hole;                    // instance number of the first hole
  struct paxos_instance *pp_first;    // closest instance to the first hole
                                      //   with instance number <= pp_inum
};

/* Sync state used by proposers during sync. */
struct paxos_sync {
  unsigned ps_total;                  // number of acceptors syncing
  unsigned ps_nacks;                  // number of sync acks
  unsigned ps_skips;                  // number of times we skipped starting
                                      //   a new sync
  paxid_t ps_hole;                    // inum of the first hole among all
                                      //   acceptors
};

/* Local state. */
struct paxos_state {
  paxid_t self_id;                    // our own acceptor ID
  paxid_t req_id;                     // local incrementing request ID
  struct paxos_acceptor *proposer;    // the acceptor we think is the proposer
  ballot_t ballot;                    // identity of the current ballot
  paxid_t istart;                     // starting point of instance numbers

  struct paxos_prep *prep;            // prepare state; NULL if not preparing
  struct paxos_sync *sync;            // sync state; NULL if not syncing
  paxid_t sync_id;                    // locally-unique sync ID

  struct acceptor_list alist;         // list of all Paxos participants
  struct instance_list ilist;         // list of all instances
  struct request_list rlist;          // queued up requests waiting for commit

  GIOChannel *(*connect)(char *, size_t); // callback for initiating connections
};

extern struct paxos_state pax;
inline int is_proposer();
inline paxid_t next_instance();

#define MAJORITY  ((LIST_COUNT(&(pax.alist)) / 2) + 1)

/* Paxos protocol. */
void paxos_init(GIOChannel *(*)(char *, size_t));
void paxos_drop_connection(struct paxos_peer *);
int paxos_request(dkind_t, const char *, size_t len);

/* Utility functions. */
int paxos_dispatch(struct paxos_peer *, const msgpack_object *);

/* Paxos message sending. */
int paxos_broadcast(const char *, size_t);
int paxos_send_to_proposer(const char *, size_t);

#endif /* __PAXOS_H__ */
