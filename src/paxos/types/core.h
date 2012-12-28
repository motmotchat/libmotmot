/**
 * core.h - Core Paxos types.
 */
#ifndef __PAXOS_TYPES_CORE_H__
#define __PAXOS_TYPES_CORE_H__

#include <assert.h>

#include "types/primitives.h"
#include "util/paxos_msgpack.h"

///////////////////////////////////////////////////////////////////////////////
//
//  Message types and headers.
//
///////////////////////////////////////////////////////////////////////////////

/* Alias ballots as (proposer ID, ballot number). */
typedef ppair_t ballot_t;

/* Paxos message types. */
typedef enum paxos_opcode {
  /* Standard protocol operations. */
  OP_PREPARE = 0,         // declare new ballot (NextBallot)
  OP_PROMISE,             // promise to ignore earlier ballots (LastVote)
  OP_DECREE,              // propose a decree (BeginBallot)
  OP_ACCEPT,              // accept a decree (Voted)
  OP_COMMIT,              // commit a decree (Success)

  /* Participant initiation. */
  OP_WELCOME,             // welcome the new acceptor into our proposership
  OP_HELLO,               // introduce ourselves after connecting

  /* Out-of-band decree requests. */
  OP_REQUEST,             // request a decree from the proposer
  OP_RETRIEVE,            // retrieve missing request data for commit
  OP_RESEND,              // resend request data

  /* Participant reconnection. */
  OP_REDIRECT,            // redirect an illigitimately preparing proposer
  OP_REFUSE,              // refuse a request due to lack of proposership
  OP_REJECT,              // reject a part decree due to live connection

  /* Retry protocol. */
  OP_RETRY,               // obtain a missing commit
  OP_RECOMMIT,            // resend a commit

  /* Log synchronization. */
  OP_SYNC,                // sync up ilists in preparation for a truncate
  OP_LAST,                // give the proposer our sync information
  OP_TRUNCATE,            // order acceptors to truncate their ilists
} paxop_t;

/* Paxos message header that is included with any message. */
struct paxos_header {
  pax_uuid_t ph_session;  // session ID
  ballot_t ph_ballot;     // ballot ID
  paxop_t ph_opcode;      // protocol opcode
  paxid_t ph_inum;        // Multi-Paxos instance number
  /**
   * The ph_inum field means different things for the different opcodes:
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

int ballot_compare(ballot_t, ballot_t);
void header_init(struct paxos_header *, paxop_t, paxid_t);

void paxos_header_pack(struct paxos_yak *, struct paxos_header *);
void paxos_header_unpack(struct paxos_header *, msgpack_object *);

///////////////////////////////////////////////////////////////////////////////
//
//  Message values.
//
///////////////////////////////////////////////////////////////////////////////

/* Alias request ID's as (from ID, local request number). */
typedef ppair_t reqid_t;

/* Kinds of decrees. */
typedef enum decree_kind {
  DEC_NULL = 0,       // null value
  DEC_CHAT,           // chat message
  DEC_JOIN,           // add an acceptor
  DEC_PART,           // remove an acceptor
  DEC_KILL            // remove an acceptor with force
} dkind_t;

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

int reqid_compare(reqid_t, reqid_t);

void paxos_value_pack(struct paxos_yak *, struct paxos_value *);
void paxos_value_unpack(struct paxos_value *, msgpack_object *);

#endif /* __PAXOS_TYPES_CORE_H__ */
