/**
 * paxos.c - Paxos protocol functions that live near the interface.
 */
#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "list.h"

#include <assert.h>
#include <glib.h>

// Local protocol state
struct paxos_state pax;

int proposer_broadcast_ihv(struct paxos_instance *);
void ilist_insert(struct paxos_instance *);
int proposer_decree_part(struct paxos_acceptor *);
int proposer_force_kill(struct paxos_peer *);

/**
 * paxos_init - Initialize local Paxos state.
 *
 * Most of our state is worthless until we are welcomed to the system.
 */
void
paxos_init(connect_t connect, struct learn_table *learn)
{
  pax.self_id = 0;
  pax.req_id = 0;
  pax.proposer = NULL;
  pax.ballot.id = 0;
  pax.ballot.gen = 0;
  pax.gen_high = 0;

  pax.ibase = 0;
  pax.ihole = 0;
  pax.istart = NULL;

  pax.prep = NULL;
  pax.sync = NULL;
  pax.sync_id = 0;

  pax.live_count = 0;
  LIST_INIT(&pax.alist);
  LIST_INIT(&pax.ilist);
  LIST_INIT(&pax.idefer);
  LIST_INIT(&pax.rcache);

  pax.connect = connect;
  pax.learn.chat = learn->chat;
  pax.learn.join = learn->join;
  pax.learn.part = learn->part;
}

/**
 * paxos_start - Start up the Paxos protocol with ourselves as the proposer.
 *
 * Currently we only support one universal execution of the protocol, so
 * everybody had better be in agreement about the fact that we get to start
 * it up.
 */
void
paxos_start(const void *desc, size_t size)
{
  struct paxos_acceptor *acc;
  struct paxos_instance *inst;

  // Give ourselves ID 1.
  pax.self_id = 1;

  // Prepare a new ballot.  Hey, we accept the prepare!  Hoorah.
  pax.ballot.id = pax.self_id;
  pax.ballot.gen = 1;
  pax.gen_high = 1;

  // Artificially generate an initial commit, without learning.
  inst = g_malloc0(sizeof(*inst));

  inst->pi_hdr.ph_ballot.id = pax.ballot.id;
  inst->pi_hdr.ph_ballot.gen = pax.ballot.gen;
  inst->pi_hdr.ph_opcode = OP_DECREE;
  inst->pi_hdr.ph_inum = 1;

  inst->pi_votes = 0;

  inst->pi_val.pv_dkind = DEC_JOIN;
  inst->pi_val.pv_reqid.id = pax.self_id;
  inst->pi_val.pv_reqid.gen = (++pax.req_id);

  // Add it to our ilist to mark our JOIN.
  LIST_INSERT_HEAD(&pax.ilist, inst, pi_le);

  // Mark the start of the instance list.
  pax.ibase = 1;

  // Set up the learn protocol parameters to start at the next instance.
  pax.ihole = 2;
  pax.istart = inst;

  // Add ourselves to the acceptor list.
  acc = g_malloc0(sizeof(*acc));
  acc->pa_paxid = pax.self_id;
  acc->pa_peer = NULL;
  acc->pa_size = size;
  acc->pa_desc = g_memdup(desc, size);
  LIST_INSERT_HEAD(&pax.alist, acc, pa_le);

  pax.live_count = 1;

  // Set ourselves as the proposer.
  pax.proposer = acc;
}

/**
 * paxos_end - End our participancy in a Paxos protocol.
 */
void
paxos_end()
{
  // Wipe all our lists.
  acceptor_list_destroy(&pax.alist);
  instance_list_destroy(&pax.ilist);
  instance_list_destroy(&pax.idefer);
  request_list_destroy(&pax.rcache);

  // Reinitialize our state.
  paxos_init(pax.connect, &pax.learn);
}

/**
 * paxos_register - Register a channel with Paxos.
 *
 * This function is called by a new client each time an acceptor first
 * initiates a connection with it.
 */
void
paxos_register_connection(GIOChannel *chan)
{
  // Just initialize a peer object.
  paxos_peer_init(chan);
}

/**
 * paxos_drop_connection - Account for a lost connection.
 *
 * We mark the acceptor as unavailable, "elect" the new president locally,
 * and start a prepare phase if necessary.
 */
void
paxos_drop_connection(struct paxos_peer *source)
{
  int was_proposer;
  struct paxos_acceptor *acc;

  // Are we the proposer right now?
  was_proposer = is_proposer();

  // Connection dropped; mark the acceptor as dead.
  LIST_FOREACH(acc, &pax.alist, pa_le) {
    if (acc->pa_peer == source) {
      paxos_peer_destroy(acc->pa_peer);
      acc->pa_peer = NULL;
      pax.live_count--;
      break;
    }
  }

  // If we are the proposer, decree a part for the acceptor.
  if (was_proposer) {
    proposer_decree_part(acc);
    return;
  }

  // Oh noes!  Did we lose the proposer?
  if (acc->pa_paxid == pax.proposer->pa_paxid) {
    // Let's mark the new one.
    reset_proposer();

    // If we're the new proposer, send a prepare.
    if (!was_proposer && is_proposer()) {
      proposer_prepare();
    }
  }
}

/**
 * proposer_dispatch - Process a message as the proposer.
 */
static int
proposer_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  int retval = 0;

  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      // Invalid system state; kill the offender.
      retval = proposer_force_kill(source);
      break;
    case OP_PROMISE:
      retval = proposer_ack_promise(hdr, o);
      break;
    case OP_DECREE:
      // Invalid system state; kill the offender.
      retval = proposer_force_kill(source);
      break;
    case OP_ACCEPT:
      retval = proposer_ack_accept(hdr);
      break;
    case OP_COMMIT:
      // XXX: Commit and relinquish presidency if the ballot is higher,
      // otherwise check if we have a decree of the given instance.  If
      // we do and /are not preparing/, redirect; otherwise (if we don't
      // or we do but we are preparing), commit it.
      // Invalid system state; kill the offender.
      retval = proposer_force_kill(source);
      break;

    case OP_REQUEST:
      retval = proposer_ack_request(source, hdr, o);
      break;
    case OP_RETRIEVE:
      retval = paxos_ack_retrieve(hdr, o);
      break;
    case OP_RESEND:
      retval = paxos_ack_resend(hdr, o);
      break;

    case OP_WELCOME:
      // Invalid system state; kill the offender.
      retval = proposer_force_kill(source);
      break;
    case OP_GREET:
      // Invalid system state; kill the offender.
      retval = proposer_force_kill(source);
      break;
    case OP_HELLO:
      // This shouldn't be possible; all the acceptors who would say hello to
      // us are higher-ranked than us when we join the protocol, and if we are
      // the proposer, they have all dropped.  Try to kill whoever sent this
      // to us.
      retval = proposer_force_kill(source);
      break;
    case OP_PTMY:
      retval = proposer_ack_ptmy(hdr);
      break;

    case OP_REDIRECT:
      retval = proposer_ack_redirect(hdr, o);
      break;
    case OP_REJECT:
      retval = proposer_ack_reject(hdr);
      break;
    case OP_REINTRO:
      retval = paxos_ack_reintro(hdr);
      break;

    case OP_SYNC:
      retval = proposer_ack_sync(hdr, o);
      break;
    case OP_TRUNCATE:
      // Invalid system state; kill the offender.
      retval = proposer_force_kill(source);
      break;
  }

  return retval;
}

/**
 * acceptor_dispatch - Process a message as an acceptor.
 */
static int
acceptor_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  int retval = 0;

  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      retval = acceptor_ack_prepare(source, hdr);
      break;
    case OP_PROMISE:
      // Ignore promises.
      break;
    case OP_DECREE:
      retval = acceptor_ack_decree(hdr, o);
      break;
    case OP_ACCEPT:
      // Ignore accepts.
      break;
    case OP_COMMIT:
      retval = acceptor_ack_commit(hdr, o);
      break;

    case OP_REQUEST:
      retval = acceptor_ack_request(source, hdr, o);
      break;
    case OP_RETRIEVE:
      retval = paxos_ack_retrieve(hdr, o);
      break;
    case OP_RESEND:
      retval = paxos_ack_resend(hdr, o);
      break;

    case OP_WELCOME:
      retval = acceptor_ack_welcome(source, hdr, o);
      break;
    case OP_GREET:
      retval = acceptor_ack_greet(hdr);
      break;
    case OP_HELLO:
      retval = acceptor_ack_hello(source, hdr);
      break;
    case OP_PTMY:
      // Ignore ptmy's.
      break;

    case OP_REDIRECT:
      retval = acceptor_ack_redirect(hdr, o);
      break;
    case OP_REJECT:
      // Ignore rejects.
      break;
    case OP_REINTRO:
      retval = paxos_ack_reintro(hdr);
      break;

    case OP_SYNC:
      retval = acceptor_ack_sync(hdr);
      break;
    case OP_TRUNCATE:
      retval = acceptor_ack_truncate(hdr, o);
      break;
  }

  return 0;
}

/**
 * paxos_dispatch - Handle a Paxos message.
 */
int
paxos_dispatch(struct paxos_peer *source, const msgpack_object *o)
{
  int retval;
  struct paxos_header *hdr;

  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size > 0 && o->via.array.size <= 2);

  // Unpack the Paxos header.  This may be clobbered by the proposer/acceptor
  // routines which the dispatch functions call.
  hdr = g_malloc0(sizeof(*hdr));
  paxos_header_unpack(hdr, o->via.array.ptr);

  // Switch on the type of message received.
  if (is_proposer()) {
    retval = proposer_dispatch(source, hdr, o->via.array.ptr + 1);
  } else {
    retval = acceptor_dispatch(source, hdr, o->via.array.ptr + 1);
  }

  g_free(hdr);
  return retval;
}


////////////////////////////////////////////////////////////////////////////////
//
//  Shared protocol helpers
//

/**
 * paxos_broadcast_ihv - Pack the header and value of an instance and
 * broadcast.
 */
int
paxos_broadcast_ihv(struct paxos_instance *inst)
{
  struct paxos_yak py;

  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_value_pack(&py, &(inst->pi_val));
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * ilist_insert - Insert a newly allocated instance into the ilist, marking
 * it as uncommitted (with one vote) and updating the hole.
 */
void
ilist_insert(struct paxos_instance *inst)
{
  // Mark one vote and as many rejects as we have dead acceptors.
  inst->pi_votes = 1;
  inst->pi_rejects = LIST_COUNT(&pax.alist) - pax.live_count;

  // Insert into the ilist.
  instance_insert(&pax.ilist, inst);

  // Update istart if we just instantiated the hole.
  if (inst->pi_hdr.ph_inum == pax.ihole) {
    pax.istart = inst;
  }
}

/**
 * proposer_decree_part - Decree a part.
 */
int
proposer_decree_part(struct paxos_acceptor *acc)
{
  struct paxos_instance *inst;

  inst = g_malloc(sizeof(*inst));

  inst->pi_val.pv_dkind = DEC_PART;
  inst->pi_val.pv_reqid.id = pax.self_id;
  inst->pi_val.pv_reqid.gen = (++pax.req_id);
  inst->pi_val.pv_extra = acc->pa_paxid;

  return proposer_decree(inst);
}

/**
 * proposer_force_kill - Somebody is misbehaving in our Paxos, and our proposer
 * will have none of it.
 *
 * We call this whenever the proposer receives a message from someone else who
 * thinks they have the right to propose.  Since the ranking for proposer
 * eligibility is the result of Paxos commits, and since we totally order all
 * learns of commits, this qualifies as an inconsistent state of the system,
 * which should not actually be possible by Paxos's correctness guarantees.
 *
 * To make a best effort at eliminating the inconsistency, we call this routine
 * to try to kill the offender.
 */
int
proposer_force_kill(struct paxos_peer *source)
{
  struct paxos_acceptor *acc;

  // Cry.
  g_critical("paxos_dispatch: Two live proposers detected.\n");

  // Find our acceptor object.
  LIST_FOREACH(acc, &pax.alist, pa_le) {
    if (acc->pa_peer == source) {
      // Decree a part for the acceptor.
      return proposer_decree_part(acc);
    }
  }

  // If we didn't find the acceptor, fail.
  return 1;
}
