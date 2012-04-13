/**
 * paxos.h - Paxos interface
 */

#include <stdint.h>
#include <string.h>

typedef uint32_t  paxid_t;

/* Totally ordered ID of a proposer / ballot. */
typedef struct proposer_id {
  paxid_t id;       // individual ID of the proposer
  paxid_t gen;      // generation number of the proposer
} proposer_t;

/* Paxos message types. */
typedef enum paxos_opcode {
  OP_PREPARE = 0,   // declare new proposership (NextBallot)
  OP_PROMISE,       // promise to ignore earlier proposers (LastVote)
  OP_DECREE,        // propose a decree (BeginBallot)
  OP_ACCEPT,        // accept a decree (Voted)
  OP_COMMIT,        // commit a decree (Success)
} paxop_t;

/* Kinds of decrees. */
typedef enum decree_kind {
  DEC_CHAT,         // chat message
  DEC_RENEW,        // proposer lease renewal
  DEC_CONFIG,       // add or remove acceptors
} dkind_t;

/* Decree value type. */
typedef struct paxos_val {
  dkind_t dkind;
  char *data;
} paxval_t;

/* Paxos message header that is included with any message. */
struct paxos_hdr {
  proposer_t pax_prop;  // full ID of the proposer (e.g, ballot)
  paxop_t pax_opcode;   // protocol opcode
  paxid_t pax_inst;     // Multi-Paxos instance number
  /**
   * The pax_inst field means different things for the different paxops:
   * - OP_PREPARE: The lowest instance for which the preparer has not committed
   *   a value.  Any acceptor who accepts the prepare will return to us the
   *   most recent value they accepted for each decree starting with pax_inst.
   * - OP_PROMISE: The index of the first vote returned (as specified in
   *   the prepare message).
   * - OP_DECREE, OP_ACCEPT, OP_COMMIT: The instance number of the decree.
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
 *       paxval_t value;
 *     } votes[];
 *   };
 *
 * - OP_DECREE: Decrees are just headers plus values.
 * - OP_ACCEPT: Acceptances are headers along with an acceptor_id.
 * - OP_COMMIT: Commits are just headers plus values.
 */
