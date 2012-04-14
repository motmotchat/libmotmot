/**
 * paxos.h - Paxos interface
 */
#ifndef __PAXOS_H__
#define __PAXOS_H__

#include "list.h"
#include <stdint.h>
#include <string.h>

#include <glib.h>
#include <msgpack.h>


/* Paxos ID type. */
typedef uint32_t  paxid_t;

/* Totally ordered ID of a proposer / ballot. */
typedef struct proposer_id {
  paxid_t id;       // individual ID of the proposer
  paxid_t gen;      // generation number of the proposer
} proposer_t;

inline int is_proposer();
int proposer_compare(proposer_t, proposer_t);

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

/* Kinds of decrees. */
typedef enum decree_kind {
  DEC_CHAT = 0,     // chat message
  DEC_RENEW,        // proposer lease renewal
  DEC_JOIN,         // add an acceptor
  DEC_LEAVE,        // remove an acceptor
} dkind_t;

/* Decree value type. */
struct paxos_val {
  dkind_t pv_dkind;    // decree kind
  paxid_t pv_paxid;    // from ID
  size_t pv_size;      // size of value
  char *pv_data;       // decree value
};

/* Paxos message header that is included with any message. */
struct paxos_hdr {
  proposer_t ph_prop;  // full ID of the proposer (e.g, ballot)
  paxop_t ph_opcode;   // protocol opcode
  paxid_t ph_inst;     // Multi-Paxos instance number
  /**
   * The pax_inst field means different things for the different ops:
   * - OP_PREPARE: The lowest instance for which the preparer has not committed
   *   a value.  Any acceptor who accepts the prepare will return to us the
   *   most recent value they accepted for each decree starting with pax_inst.
   * - OP_PROMISE: The index of the first vote returned (as specified in
   *   the prepare message).
   * - OP_DECREE, OP_ACCEPT, OP_COMMIT: The instance number of the decree.
   * - OP_REQUEST, OP_REDIRECT: Not used.
   */
};

/**
 * We describe the particular message formats for each Paxos message type:
 *
 * - OP_PREPARE: Nothing needed but the header.
 * - OP_PROMISE: We use the following datatype directly from the msgpack buffer:
 *
 *   struct paxos_promise {
 *     paxid_t acceptor_id;
 *     struct {
 *       proposer_t proposer;
 *       struct paxos_val value;
 *     } votes[];
 *   };
 *
 * - OP_DECREE: Decrees are just headers plus values.
 * - OP_ACCEPT: Acceptances are headers along with an acceptor_id.
 * - OP_COMMIT: Commits are just headers plus values.
 * - OP_REQUEST: Requests are just headers plus values.
 * - OP_REDIRECT: Redirects are just headers.
 */

/* Msgpack helpers. */
void msgpack_pack_paxid(msgpack_packer *, paxid_t);
void paxos_hdr_pack(msgpack_packer *, struct paxos_hdr *);
struct paxos_hdr *paxos_hdr_unpack(msgpack_object *);
void paxos_val_pack(msgpack_packer *, struct paxos_val *);
struct paxos_val *paxos_val_unpack(msgpack_object *);

/* Internal representation of a decree. */
struct paxos_decree {
  struct paxos_hdr pd_hdr;  // Paxos header identifying the decree
  unsigned pd_votes;        // number of accepts (proposer use only)
  LIST_ENTRY(paxos_decree) pd_list;   // sorted linked list of decrees
  struct paxos_val pd_val;  // value of the decree
};

LIST_HEAD(decree_list, paxos_decree);

struct paxos_decree *decree_find(struct decree_list *, paxid_t);
int decree_add(struct decree_list *, struct paxos_hdr *, struct paxos_val *);

/* Paxos participant list element. */
struct paxipant {
  paxid_t px_paxid;
  LIST_ENTRY(paxipant) px_le;
};

/* List of acceptors' promises for a single Paxos instance / decree. */
struct prepinst {
  paxid_t inst;
  LIST_HEAD(, decree_list) dlist;
  LIST_ENTRY(prepinst) le;
};

/* Preparation state for new proposers. */
struct paxos_prep {
  unsigned pp_npreps;
  LIST_HEAD(, prepinst) pp_pinsts;
};

/* Local state. */
struct paxos_state {
  paxid_t self_id;              // our own acceptor ID
  proposer_t proposer;          // identity of the proposer we recognize
  struct paxos_prep *prep;      // preparation state; NULL if not preparing
  struct decree_list dlist;     // list of pending decrees
  LIST_HEAD(, paxipant) plist;  // list of other Paxos participants
};

extern struct paxos_state pax;

/* Paxos protocol. */
int paxos_dispatch(GIOChannel *, GIOCondition, void *);

#endif /* __PAXOS_H__ */
