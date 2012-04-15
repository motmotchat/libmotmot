/**
 * paxos.h - Paxos interface
 */
#ifndef __PAXOS_H__
#define __PAXOS_H__

#include "list.h"

#include <stdint.h>
#include <string.h>
#include <glib.h>

/* Paxos ID type. */
typedef uint32_t  paxid_t;

/* Totally ordered ID of a ballot. */
typedef struct ballot {
  paxid_t id;       // ID of the proposer
  paxid_t gen;      // generation number of the ballot
} ballot_t;

int ballot_compare(ballot_t, ballot_t);

/* Paxos message types. */
typedef enum paxos_opcode {
  OP_PREPARE = 0,   // declare new proposership (NextBallot)
  OP_PROMISE,       // promise to ignore earlier proposers (LastVote)
  OP_DECREE,        // propose a decree (BeginBallot)
  OP_ACCEPT,        // accept a decree (Voted)
  OP_COMMIT,        // commit a decree (Success)
  OP_REQUEST,       // request a decree from the proposer
  OP_REDIRECT,      // suggests the true identity of the proposer
} paxop_t;

/* Paxos message header that is included with any message. */
struct paxos_hdr {
  ballot_t ph_ballot;  // ballot ID
  paxop_t ph_opcode;   // protocol opcode
  paxid_t ph_inum;     // Multi-Paxos instance number
  /**
   * The ph_inum field means different things for the different ops:
   * - OP_PREPARE: The lowest instance for which the preparer has not committed
   *   a value.  Any acceptor who accepts the prepare will return to us the
   *   most recent value they accepted for each Paxos instance they participated
   *   in, starting with ph_inum.
   * - OP_PROMISE: The index of the first vote returned (as specified in
   *   the prepare message).
   * - OP_DECREE, OP_ACCEPT, OP_COMMIT: The instance number of the decree.
   * - OP_REQUEST, OP_REDIRECT: Not used.
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
 *     struct paxos_val val;
 *   } votes[];
 *
 * - OP_DECREE: Decrees are headers plus values.
 * - OP_ACCEPT: Acceptances are headers.
 * - OP_COMMIT: Commits are just headers.
 * - OP_REQUEST: Requests are headers plus values.
 * - OP_REDIRECT: Redirects are just headers.
 */

/* Kinds of decrees. */
typedef enum decree_kind {
  DEC_NULL = 0,   // null value
  DEC_CHAT,       // chat message
  DEC_RENEW,      // proposer lease renewal
  DEC_JOIN,       // add an acceptor
  DEC_LEAVE,      // remove an acceptor
} dkind_t;

/* Decree value type. */
struct paxos_val {
  dkind_t pv_dkind;    // decree kind
  paxid_t pv_paxid;    // from ID
  size_t pv_size;      // size of value
  char *pv_data;       // decree value
};

/* Representation of a Paxos instance. */
struct paxos_instance {
  struct paxos_hdr pi_hdr;  // Paxos header identifying the instance
  unsigned pi_votes;        // number of accepts -OR- 0 if committed
  LIST_ENTRY(paxos_instance) pi_le;   // sorted linked list of instances
  struct paxos_val pi_val;  // value of the decree
};

LIST_HEAD(instance_list, paxos_instance);

void instance_free(struct paxos_instance *);
struct paxos_instance *instance_find(struct instance_list *, paxid_t);
struct paxos_instance *instance_add(struct instance_list *,
    struct paxos_instance *);

/* A Paxos protocol participant. */
struct paxos_acceptor {
  paxid_t pa_paxid;         // agent's ID
  GIOChannel *pa_chan;      // agent's channel; NULL if we think it's dead
  LIST_ENTRY(paxos_acceptor) pa_le;   // sorted linked list of all participants
};

#define MAJORITY  ((LIST_COUNT(&(pax.alist)) / 2) + 1)

/* Preparation state for new proposers. */
struct paxos_prep {
  unsigned pp_nacks;    // number of prepare acks
  paxid_t pp_inum;      // instance number of the first hole
  struct paxos_instance *pp_first;  // closest instance to the first hole
                                    // with instance number <= pp_inum
};

/* Local state. */
struct paxos_state {
  paxid_t self_id;                    // our own acceptor ID
  struct paxos_acceptor *proposer;    // the acceptor we think is the proposer
  ballot_t ballot;                    // identity of the current ballot
  struct paxos_prep *prep;            // prepare state; NULL if not preparing
  struct instance_list ilist;         // list of all instances
  LIST_HEAD(, paxos_acceptor) alist;  // list of all Paxos participants
};

extern struct paxos_state pax;
inline int is_proposer();
inline paxid_t next_instance();

/* Paxos protocol. */
int paxos_dispatch(GIOChannel *, GIOCondition, void *);
int paxos_broadcast(char *, size_t);

#endif /* __PAXOS_H__ */
