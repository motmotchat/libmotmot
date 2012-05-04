/**
 * paxos.h - Paxos interface
 */
#ifndef __PAXOS_H__
#define __PAXOS_H__

#include "list.h"
#include "motmot.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <msgpack.h>
#include <glib.h>

/* Paxos ID type. */
typedef uint32_t  paxid_t;
typedef paxid_t   uuid_t;

/* Totally ordered pair of paxid's. */
typedef struct paxid_pair {
  paxid_t id;           // ID of participant
  paxid_t gen;          // generation number of some sort
} ppair_t;

/* Alias ballots as (proposer ID, ballot number). */
typedef ppair_t ballot_t;

/* Paxos message types. */
typedef enum paxos_opcode {
  /* Standard protocol operations. */
  OP_PREPARE = 0,       // declare new ballot (NextBallot)
  OP_PROMISE,           // promise to ignore earlier ballots (LastVote)
  OP_DECREE,            // propose a decree (BeginBallot)
  OP_ACCEPT,            // accept a decree (Voted)
  OP_COMMIT,            // commit a decree (Success)

  /* Participant initiation. */
  OP_WELCOME,           // welcome the new acceptor into our proposership
  OP_HELLO,             // introduce ourselves after connecting

  /* Out-of-band decree requests. */
  OP_REQUEST,           // request a decree from the proposer
  OP_RETRIEVE,          // retrieve missing request data for commit
  OP_RESEND,            // resend request data

  /* Participant reconnection. */
  OP_REDIRECT,          // redirect an illigitimately preparing proposer
  OP_REFUSE,            // refuse a request due to lack of proposership
  OP_REJECT,            // reject a part decree due to live connection

  /* Retry protocol. */
  OP_RETRY,             // obtain a missing commit
  OP_RECOMMIT,          // resend a commit

  /* Log synchronization. */
  OP_SYNC,              // sync up ilists in preparation for a truncate
  OP_LAST,              // give the proposer our sync information
  OP_TRUNCATE,          // order acceptors to truncate their ilists
} paxop_t;

/* Paxos message header that is included with any message. */
struct paxos_header {
  uuid_t ph_session;    // session ID
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
   * - OP_PROMISE: The index of the first vote returned (echoed from the
   *   prepare message).
   *
   * - OP_DECREE, OP_ACCEPT, OP_COMMIT: The instance number of the decree.
   *
   * - OP_WELCOME: The new acceptor's assigned paxid (which is, in fact, the
   *   instance number of its JOIN).
   *
   * - OP_HELLO: The ID of the greeter.
   *
   * - OP_REQUEST: The paxid of the acceptor who we think is the proposer who
   *   will send our request.  This allows us to send a redirect appropriately.
   *
   * - OP_RETRIEVE, OP_RESEND: The instance number associated with the
   *   desired request.
   *
   * - OP_REDIRECT, OP_REFUSE: The ID of the proposer we are redirecting to.
   *
   * - OP_REJECT: The instance number of the decree.
   *
   * - OP_RETRY, OP_RECOMMIT: The instance number of the decree.
   *
   * - OP_SYNC, OP_LAST, OP_TRUNCATE: The ID of the sync as determined by the
   *   proposer; this is used only by the proposer and is simply echoed across
   *   all messages in the sync operation.
   *
   * Note that ALL of our ID's start counting at 1; 0 is always a sentinel
   * value.
   */
};

/**
 * We describe the wire protocol for our Paxos system
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

/* Kinds of decrees. */
typedef enum decree_kind {
  DEC_NULL = 0,       // null value
  DEC_CHAT,           // chat message
  DEC_JOIN,           // add an acceptor
  DEC_PART,           // remove an acceptor
} dkind_t;

/* Alias request ID's as (from ID, local request number). */
typedef ppair_t reqid_t;

/* Decree value type. */
struct paxos_value {
  dkind_t pv_dkind;   // decree kind
  reqid_t pv_reqid;   // totally ordered request ID
  paxid_t pv_extra;   // we get one 32-bit data value (mostly for PART)
  /**
   * In order to reduce network traffic, requesters broadcast any requests
   * carrying nontrivial data to all acceptors, associating with each a
   * session-unique ID (the combination of the requester's acceptor ID with
   * an incrementing requester-local request number).  Any data they pass
   * along is cached by the acceptors.  The proposer then makes decrees and
   * orders commits with values taking the form of this request ID.
   */
};

/* A Paxos protocol participant. */
struct paxos_acceptor {
  paxid_t pa_paxid;                   // agent's ID; also the instance number of
                                      //   the agent's JOIN decree
  struct paxos_peer *pa_peer;         // agent's connection information; NULL if
                                      //   we think it's dead
  size_t pa_size;                     // size of identity data
  void *pa_desc;                      // join-time client-supplied descriptor
  LIST_ENTRY(paxos_acceptor) pa_le;   // sorted linked list of all participants
};
LIST_HEAD(acceptor_list, paxos_acceptor);

/* An instance of the "synod" algorithm. */
struct paxos_instance {
  struct paxos_header pi_hdr;         // Paxos header identifying the instance
  bool pi_committed;                  // true if a commit has been received
  bool pi_cached;                     // true if the request is cached; not sent
  bool pi_learned;                    // true if learned; not sent
  unsigned pi_votes;                  // number of accepts; not sent
  unsigned pi_rejects;                // number of rejects; not sent
  LIST_ENTRY(paxos_instance) pi_le;   // sorted linked list of instances
  struct paxos_value pi_val;          // value of the decree
};
LIST_HEAD(instance_list, paxos_instance);

/* Request containing data, pending proposer commit. */
struct paxos_request {
  struct paxos_value pr_val;          // request ID and kind
  size_t pr_size;                     // size of data
  void *pr_data;                      // data pointer dependent on kind
  LIST_ENTRY(paxos_request) pr_le;    // sorted linked list of requests
};
LIST_HEAD(request_list, paxos_request);

/* Preparation state used by new proposers. */
struct paxos_prep {
  ballot_t pp_ballot;                 // ballot being prepared
  unsigned pp_acks;                   // number of prepare acks
  unsigned pp_redirects;              // number of prepare rejects
};

/* Sync state used by proposers during sync. */
struct paxos_sync {
  unsigned ps_total;      // number of acceptors syncing
  unsigned ps_acks;       // number of sync acks
  unsigned ps_skips;      // number of times we skipped starting a new sync
  paxid_t ps_last;        // the last contiguous learn across the system
};

/* Continuation-style callbacks for connect_t calls. */
struct paxos_connectinue {
  struct motmot_connect_cb pc_cb;
  paxid_t pc_paxid;
  paxid_t pc_inum;
  LIST_ENTRY(paxos_connectinue) pc_le;
};
LIST_HEAD(continue_list, paxos_connectinue);

/* Session state. */
struct paxos_session {
  uuid_t session_id;                  // ID of the Paxos session
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
  struct acceptor_list alist;         // list of all Paxos participants
  struct acceptor_list adefer;        // list of deferred hello acks
  struct continue_list connectinues;  // list of connectinuations

  struct instance_list ilist;         // list of all instances
  struct instance_list idefer;        // list of deferred instances
  struct request_list rcache;         // cached requests waiting for commit

  paxid_t ibase;                      // base value for instance numbers
  paxid_t ihole;                      // number of first uncommitted instance
  struct paxos_instance *istart;      // lower bound instance of first hole

  LIST_ENTRY(paxos_session) le;       // session list entry
};
LIST_HEAD(session_list, paxos_session);

/* Table of client learning callbacks. */
struct learn_table {
  learn_t chat;
  learn_t join;
  learn_t part;
};

/* Global state. */
struct paxos_state {
  connect_t connect;                  // callback for initiating connections
  enter_t enter;                      // callback for entering chat
  leave_t leave;                      // callback for leaving chat
  struct learn_table learn;           // callbacks for paxos_learn
};

extern struct paxos_state state;
extern struct paxos_session *pax;

/* Paxos protocol interface. */
int paxos_init(connect_t, struct learn_table *, enter_t, leave_t);
void *paxos_start(const void *, size_t, void *);
int paxos_end(void *data);

int paxos_register_connection(GIOChannel *);
int paxos_drop_connection(struct paxos_peer *);

int paxos_request(struct paxos_session *, dkind_t, const void *, size_t len);
int paxos_dispatch(struct paxos_peer *, const msgpack_object *);
int paxos_sync(void *);

#endif /* __PAXOS_H__ */
