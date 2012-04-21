/**
 * paxos.c - Paxos protocol functions that live near the interface.
 */
#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_msgpack.h"
#include "paxos_protocol.h"
#include "list.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <glib.h>

// Local protocol state
struct paxos_state pax;

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

  pax.ibase = 0;
  pax.ihole = 0;
  pax.istart = NULL;

  pax.prep = NULL;
  pax.sync = NULL;
  pax.sync_id = 0;

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

  // Initialize an initial commit.
  inst = g_malloc(sizeof(*inst));

  inst->pi_hdr.ph_ballot.id = pax.ballot.id;
  inst->pi_hdr.ph_ballot.gen = pax.ballot.gen;
  inst->pi_hdr.ph_opcode = OP_DECREE;
  inst->pi_hdr.ph_inum = 1;

  inst->pi_votes = 0;

  inst->pi_val.pv_dkind = DEC_JOIN;
  inst->pi_val.pv_reqid.id = pax.self_id;
  inst->pi_val.pv_reqid.gen = pax.req_id;

  // Add it to our ilist to mark our JOIN.
  LIST_INSERT_HEAD(&pax.ilist, inst, pi_le);
  pax.ibase = 1;
  pax.ihole = 2;
  pax.istart = inst;

  // Add ourselves to the acceptor list.
  acc = g_malloc(sizeof(*acc));
  acc->pa_paxid = pax.self_id;
  acc->pa_peer = NULL;
  acc->pa_size = size;
  acc->pa_desc = g_memdup(desc, size);
  LIST_INSERT_HEAD(&pax.alist, acc, pa_le);

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

static int
proposer_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      // If this happens, it means that two proposers share a live connection
      // yet both think that they are at the head of the list of acceptors.
      // This list is the result of iterative Paxos commits, and hence a
      // proposer receiving an OP_PREPARE qualifies as an inconsistent state
      // of the system.
      g_error("Proposer received OP_PREPARE.\n");
      break;
    case OP_PROMISE:
      proposer_ack_promise(hdr, o);
      break;
    case OP_DECREE:
      // XXX: If the decree is for a higher ballot number, we should probably
      // cry.
      g_error("Bad opcode OP_DECREE recieved by proposer. Redirecting...\n");
      paxos_redirect(source, hdr);
      break;
    case OP_ACCEPT:
      proposer_ack_accept(source, hdr);
      break;
    case OP_COMMIT:
      // TODO: Commit and relinquish presidency if the ballot is higher,
      // otherwise check if we have a decree of the given instance.  If
      // we do and /are not preparing/, redirect; otherwise (if we don't
      // or we do but we are preparing), commit it.
      break;

    case OP_REQUEST:
      proposer_ack_request(source, hdr, o);
      break;
    case OP_RETRIEVE:
      paxos_ack_retrieve(hdr, o);
      break;
    case OP_RESEND:
      paxos_ack_resend(hdr, o);
      break;

    case OP_WELCOME:
      // Ignore.
      break;
    case OP_GREET:
      // Ignore.
      break;
    case OP_HELLO:
      // This shouldn't be possible; all the acceptors who would say hello to
      // us are higher-ranked than us when we join the protocol, and if we are
      // the proposer, they have all dropped.
      g_error("Proposer received OP_HELLO.\n");
      break;
    case OP_PTMY:
      proposer_ack_ptmy(hdr);
      break;

    case OP_REDIRECT:
      // TODO: Decide what to do.
      break;
    case OP_SYNC:
      proposer_ack_sync(hdr, o);
      break;
    case OP_TRUNCATE:
      // Holy fucking shit what is happening.
      g_error("Proposer received OP_TRUNCATE.\n");
      break;
  }

  return 0;
}

static int
acceptor_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      acceptor_ack_prepare(source, hdr);
      break;
    case OP_PROMISE:
      g_error("Bad opcode OP_PROMISE recieved by acceptor. Redirecting...\n");
      paxos_redirect(source, hdr);
      break;
    case OP_DECREE:
      acceptor_ack_decree(source, hdr, o);
      break;
    case OP_ACCEPT:
      g_error("Bad opcode OP_ACCEPT recieved by acceptor. Redirecting...\n");
      paxos_redirect(source, hdr);
      break;
    case OP_COMMIT:
      acceptor_ack_commit(hdr, o);
      break;

    case OP_REQUEST:
      acceptor_ack_request(source, hdr, o);
      break;
    case OP_RETRIEVE:
      paxos_ack_retrieve(hdr, o);
      break;
    case OP_RESEND:
      paxos_ack_resend(hdr, o);
      break;

    case OP_WELCOME:
      acceptor_ack_welcome(source, hdr, o);
      break;
    case OP_GREET:
      acceptor_ack_greet(hdr);
      break;
    case OP_HELLO:
      acceptor_ack_hello(source, hdr);
      break;
    case OP_PTMY:
      // Ignore.
      break;

    case OP_REDIRECT:
      // TODO: Think Real Hard (tm)
      break;
    case OP_SYNC:
      acceptor_ack_sync(hdr);
      break;
    case OP_TRUNCATE:
      acceptor_ack_truncate(hdr, o);
      break;
  }

  return 0;
}

/**
 * Handle a Paxos message.
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
  struct paxos_acceptor *it;

  // Are we the proposer right now?
  was_proposer = is_proposer();

  // Connection dropped; mark the acceptor as dead.
  LIST_FOREACH(it, &pax.alist, pa_le) {
    if (it->pa_peer == source) {
      paxos_peer_destroy(it->pa_peer);
      it->pa_peer = NULL;
      break;
    }
  }

  // Oh noes!  Did we lose the proposer?
  if (it->pa_paxid == pax.proposer->pa_paxid) {
    // Let's mark the new one.
    reset_proposer();

    // If we're the new proposer, send a prepare.
    if (!was_proposer && is_proposer()) {
      proposer_prepare();
    }
  }
}

/**
 * paxos_learn - Do something useful with the value of a commit.
 *
 * Note that we cannot free up the instance or any request associated with
 * it until a sync.
 */
int
paxos_learn(struct paxos_instance *inst)
{
  int was_proposer, deferred;
  struct paxos_request *req = NULL;
  struct paxos_acceptor *acc;

  // Pull the request from the request cache if applicable.
  if (request_needs_cached(inst->pi_val.pv_dkind)) {
    req = request_find(&pax.rcache, inst->pi_val.pv_reqid);
    if (req == NULL) {
      // Send out a retrieve to the request originator and defer the commit.
      return paxos_retrieve(inst);
    }
  }

  // Act on the decree (e.g., display chat, record acceptor list changes).
  switch (inst->pi_val.pv_dkind) {
    case DEC_NULL:
      break;

    case DEC_CHAT:
      // Invoke client learning callback.
      pax.learn.chat(req->pr_data, req->pr_size);
      break;

    case DEC_JOIN:
      // Finding an existing acceptor object with the new acceptor's ID means
      // that we received a greet order from the proposer for this join before
      // we actually committed it.  In this case, we do not reinitialize and
      // send out the deferred hello.
      acc = acceptor_find(&pax.alist, inst->pi_hdr.ph_inum);
      deferred = (acc != NULL);

      // If we don't find anything, initialize a new acceptor struct.  Its
      // paxid is the instance number of the join.
      if (!deferred) {
        acc = g_malloc0(sizeof(*acc));
        acc->pa_paxid = inst->pi_hdr.ph_inum;
        acceptor_insert(&pax.alist, acc);
      }

      // Initialize a paxos_peer via a callback.
      acc->pa_peer = paxos_peer_init(pax.connect(req->pr_data, req->pr_size));

      // Copy over the identity information.
      acc->pa_size = req->pr_size;
      acc->pa_desc = g_memdup(req->pr_data, req->pr_size);

      // If we are the proposer, send the new acceptor its paxid.
      if (is_proposer()) {
        proposer_welcome(acc);
      }

      // Send the deferred hello if necessary.
      if (deferred) {
        acceptor_hello(acc);
      }

      // Invoke client learning callback.
      pax.learn.join(req->pr_data, req->pr_size);
      break;

    case DEC_PART:
      // The pv_extra field tells us who is parting (possibly a forced part).
      // If it is 0, it is a user request to self-part.
      if (inst->pi_val.pv_extra == 0) {
        inst->pi_val.pv_extra = inst->pi_val.pv_reqid.id;
      }

      // Are we the proposer right now?
      was_proposer = is_proposer();

      // Pull the acceptor from the alist.
      acc = acceptor_find(&pax.alist, inst->pi_val.pv_extra);

      // Invoke client learning callback.
      pax.learn.part(acc->pa_desc, acc->pa_size);

      if (acc->pa_paxid != pax.self_id) {
        // Just clean up the acceptor.
        LIST_REMOVE(&pax.alist, acc, pa_le);
        acceptor_destroy(acc);
      } else {
        // We are leaving the protocol, so wipe all our state clean.
        paxos_end();
      }

      // Prepare if we became the proposer as a result of the part.
      reset_proposer();
      if (!was_proposer && is_proposer()) {
        proposer_prepare();
      }

      break;
  }

  return 0;
}
