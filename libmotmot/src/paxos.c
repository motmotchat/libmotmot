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

// Global system state.
struct paxos_state state;

// Current session.
struct paxos_session *pax;

void instance_insert_and_upstart(struct paxos_instance *);
int proposer_broadcast_instance(struct paxos_instance *);
int proposer_decree_part(struct paxos_acceptor *);
int proposer_force_kill(struct paxos_peer *);

/**
 * paxos_init - Initialize local Paxos state.
 *
 * Most of our state is worthless until we are welcomed to the system.
 */
int
paxos_init(connect_t connect, disconnect_t disconnect,
    struct learn_table *learn)
{
  state.connect = connect;
  state.disconnect = disconnect;
  state.learn.chat = learn->chat;
  state.learn.join = learn->join;
  state.learn.part = learn->part;

  pax = g_malloc0(sizeof(*pax));

  pax->self_id = 0;
  pax->req_id = 0;
  pax->proposer = NULL;
  pax->ballot.id = 0;
  pax->ballot.gen = 0;

  pax->ibase = 0;
  pax->ihole = 0;
  pax->istart = NULL;

  pax->gen_high = 0;
  pax->prep = NULL;

  pax->sync_id = 0;
  pax->sync_prev = 0;
  pax->sync = NULL;

  pax->live_count = 0;
  LIST_INIT(&pax->alist);
  LIST_INIT(&pax->adefer);

  LIST_INIT(&pax->ilist);
  LIST_INIT(&pax->idefer);
  LIST_INIT(&pax->rcache);

  return 0;
}

/**
 * paxos_start - Start up the Paxos protocol with ourselves as the proposer.
 */
void *
paxos_start(const void *desc, size_t size, void *data)
{
  struct paxos_request *req;
  struct paxos_instance *inst;
  struct paxos_acceptor *acc;

  // Initialize a new session.
  // XXX: Do that.
  pax->client_data = data;

  // Give ourselves ID 1.
  pax->self_id = 1;

  // Prepare a new ballot.  Hey, we accept the prepare!  Hoorah.
  pax->ballot.id = pax->self_id;
  pax->ballot.gen = 1;
  pax->gen_high = 1;

  // Submit a join request to the cache.
  req = g_malloc0(sizeof(*req));

  req->pr_val.pv_dkind = DEC_JOIN;
  req->pr_val.pv_reqid.id = pax->self_id;
  req->pr_val.pv_reqid.gen = (++pax->req_id);

  req->pr_size = size;
  req->pr_data = g_memdup(desc, size);

  LIST_INSERT_TAIL(&pax->rcache, req, pr_le);

  // Artificially generate an initial commit, without learning.
  inst = g_malloc0(sizeof(*inst));

  inst->pi_hdr.ph_ballot.id = pax->ballot.id;
  inst->pi_hdr.ph_ballot.gen = pax->ballot.gen;
  inst->pi_hdr.ph_opcode = OP_DECREE;
  inst->pi_hdr.ph_inum = 1;

  inst->pi_committed = true;
  inst->pi_cached = true;
  inst->pi_learned = true;

  inst->pi_votes = 1;
  inst->pi_rejects = 0;

  memcpy(&inst->pi_val, &req->pr_val, sizeof(req->pr_val));

  LIST_INSERT_HEAD(&pax->ilist, inst, pi_le);

  pax->ibase = 1;

  // Set up the learn protocol parameters to start at the next instance.
  pax->ihole = 2;
  pax->istart = inst;

  // Add ourselves to the acceptor list.
  acc = g_malloc0(sizeof(*acc));
  acc->pa_paxid = pax->self_id;
  acc->pa_peer = NULL;
  acc->pa_size = size;
  acc->pa_desc = g_memdup(desc, size);
  LIST_INSERT_HEAD(&pax->alist, acc, pa_le);

  pax->live_count = 1;

  // Set ourselves as the proposer.
  pax->proposer = acc;

  return &pax;
}

/**
 * paxos_end - End our participancy in a Paxos protocol.
 */
int
paxos_end(void *session)
{
  // Wipe all our lists.
  acceptor_list_destroy(&pax->alist);
  acceptor_list_destroy(&pax->adefer);
  instance_list_destroy(&pax->ilist);
  instance_list_destroy(&pax->idefer);
  request_list_destroy(&pax->rcache);

  // Reinitialize our state.
  paxos_init(state.connect, state.disconnect, &state.learn);

  return 0;
}

/**
 * paxos_register_connection - Register a channel with Paxos.
 *
 * This function is called whenever somebody tries to connect to us.
 */
int
paxos_register_connection(GIOChannel *chan)
{
  // Just initialize a peer object.
  if (paxos_peer_init(chan) == NULL) {
    return 1;
  } else {
    return 0;
  }
}

/**
 * paxos_drop_connection - Account for a lost connection.
 *
 * We mark the acceptor as unavailable, "elect" the new president locally,
 * and start a prepare phase if necessary.
 */
int
paxos_drop_connection(struct paxos_peer *source)
{
  int was_proposer;
  struct paxos_acceptor *acc;

  // Are we the proposer right now?
  was_proposer = is_proposer();

  // Connection dropped; mark the acceptor as dead.
  LIST_FOREACH(acc, &pax->alist, pa_le) {
    if (acc->pa_peer == source) {
      paxos_peer_destroy(acc->pa_peer);
      acc->pa_peer = NULL;
      pax->live_count--;
      break;
    }
  }

  // If we are the proposer, decree a part for the acceptor.
  if (was_proposer) {
    return proposer_decree_part(acc);
  }

  // Oh noes!  Did we lose the proposer?
  if (acc->pa_paxid == pax->proposer->pa_paxid) {
    // Let's mark the new one.
    reset_proposer();

    // If we're the new proposer, send a prepare.
    if (!was_proposer && is_proposer()) {
      return proposer_prepare();
    }
  }

  return 0;
}

/**
 * proposer_dispatch - Process a message as the proposer.
 */
static int
proposer_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  int r = 0;

#ifdef DEBUG
  paxos_header_print(hdr, "P: ", "\n");
#endif

  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
    case OP_PROMISE:
      r = proposer_ack_promise(hdr, o);
      break;
    case OP_DECREE:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
    case OP_ACCEPT:
      r = proposer_ack_accept(hdr);
      break;
    case OP_COMMIT:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;

    case OP_REQUEST:
      r = proposer_ack_request(hdr, o);
      break;
    case OP_RETRIEVE:
      r = paxos_ack_retrieve(hdr, o);
      break;
    case OP_RESEND:
      r = paxos_ack_resend(hdr, o);
      break;

    case OP_WELCOME:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
    case OP_HELLO:
      r = paxos_ack_hello(source, hdr);
      break;

    case OP_REDIRECT:
      r = proposer_ack_redirect(hdr, o);
      break;
    case OP_REJECT:
      r = proposer_ack_reject(hdr);
      break;

    case OP_RETRY:
      r = proposer_ack_retry(hdr);
      break;
    case OP_RECOMMIT:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;

    case OP_SYNC:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
    case OP_LAST:
      r = proposer_ack_last(hdr, o);
      break;
    case OP_TRUNCATE:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
  }

  return r;
}

/**
 * acceptor_dispatch - Process a message as an acceptor.
 */
static int
acceptor_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  int r = 0;

#ifdef DEBUG
  paxos_header_print(hdr, "A: ", "\n");
#endif

  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      r = acceptor_ack_prepare(source, hdr);
      break;
    case OP_PROMISE:
      // Ignore promises.
      break;
    case OP_DECREE:
      r = acceptor_ack_decree(hdr, o);
      break;
    case OP_ACCEPT:
      // Ignore accepts.
      break;
    case OP_COMMIT:
      r = acceptor_ack_commit(hdr, o);
      break;

    case OP_REQUEST:
      r = acceptor_ack_request(source, hdr, o);
      break;
    case OP_RETRIEVE:
      r = paxos_ack_retrieve(hdr, o);
      break;
    case OP_RESEND:
      r = paxos_ack_resend(hdr, o);
      break;

    case OP_WELCOME:
      r = acceptor_ack_welcome(source, hdr, o);
      break;
    case OP_HELLO:
      r = paxos_ack_hello(source, hdr);
      break;

    case OP_REDIRECT:
      r = acceptor_ack_redirect(hdr, o);
      break;
    case OP_REJECT:
      // Ignore rejects.
      break;

    case OP_RETRY:
      // Ignore retries.
      break;
    case OP_RECOMMIT:
      r = acceptor_ack_recommit(hdr, o);
      break;

    case OP_SYNC:
      r = acceptor_ack_sync(hdr);
      break;
    case OP_LAST:
      // Ignore lasts.
      break;
    case OP_TRUNCATE:
      r = acceptor_ack_truncate(hdr, o);
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
  int r;
  struct paxos_header *hdr;

  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size > 0 && o->via.array.size <= 2);

  // Unpack the Paxos header.  This may be clobbered by the proposer/acceptor
  // routines which the dispatch functions call.
  hdr = g_malloc0(sizeof(*hdr));
  paxos_header_unpack(hdr, o->via.array.ptr);

  // Switch on the type of message received.
  if (is_proposer()) {
    r = proposer_dispatch(source, hdr, o->via.array.ptr + 1);
  } else {
    r = acceptor_dispatch(source, hdr, o->via.array.ptr + 1);
  }

  g_free(hdr);
  return r;
}


////////////////////////////////////////////////////////////////////////////////
//
//  Shared protocol helpers
//

/**
 * instance_insert_and_upstart - Insert a newly allocated instance into the
 * ilist and update istart.
 */
void
instance_insert_and_upstart(struct paxos_instance *inst)
{
  // Insert into the ilist.
  instance_insert(&pax->ilist, inst);

  // Update istart if we just instantiated the hole.
  if (inst->pi_hdr.ph_inum == pax->ihole) {
    pax->istart = inst;
  }
}

/**
 * paxos_broadcast_instance - Pack the header and value of an instance and
 * broadcast.
 */
int
paxos_broadcast_instance(struct paxos_instance *inst)
{
  int r;
  struct paxos_yak py;

  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_value_pack(&py, &(inst->pi_val));
  r = paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * proposer_decree_part - Decree a part, deferring as necessary.
 */
int
proposer_decree_part(struct paxos_acceptor *acc)
{
  struct paxos_instance *inst;

  inst = g_malloc0(sizeof(*inst));

  inst->pi_val.pv_dkind = DEC_PART;
  inst->pi_val.pv_reqid.id = pax->self_id;
  inst->pi_val.pv_reqid.gen = (++pax->req_id);
  inst->pi_val.pv_extra = acc->pa_paxid;

  if (pax->prep != NULL) {
    LIST_INSERT_TAIL(&pax->idefer, inst, pi_le);
    return 0;
  } else {
    return proposer_decree(inst);
  }
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
  g_critical("paxos_dispatch: Two live proposers detected.");

  // Find our acceptor object.
  LIST_FOREACH(acc, &pax->alist, pa_le) {
    if (acc->pa_peer == source) {
      // Decree a part for the acceptor.
      return proposer_decree_part(acc);
    }
  }

  // If we didn't find the acceptor, fail.
  return 1;
}
