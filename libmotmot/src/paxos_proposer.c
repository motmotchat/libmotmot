/**
 * paxos_proposer.c - Proposer protocol functions for Paxos.
 */

#include <assert.h>
#include <glib.h>

#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "paxos_util.h"
#include "list.h"

static inline void
swap(void **p1, void **p2)
{
  void *tmp = *p1;
  *p1 = *p2;
  *p2 = tmp;
}

/**
 * proposer_prepare - Broadcast a prepare message to all acceptors.
 *
 * The initiation of a prepare phase is only allowed if we believe ourselves
 * to be the proposer.  Once we start a prepare phase, we wait until a majority
 * either accepts our prepare our redirects us to a higher-ranked acceptor.
 * In the latter case, if we cannot reconnect with the purported proposer,
 * we try again to prepare.
 *
 * Thus, we call proposer_prepare() iff we believe ourselves to be the
 * proposer, and we do not stop the cycle of waiting on responses and starting
 * a new prepare until we either connect to a proposer or our prepare is
 * accepted by a majority.
 *
 * Whenever a prepare succeeds, the new proposer kills off any higher-ranked
 * acceptors that remain.  This allows us to provide a useful system invariant:
 *
 *  ===========================================================================
 *    ONCE A PROPOSER SUCCESSFULLY PREPARES, IT REMAINS THE PROPOSER UNTIL IT
 *    DROPS OUT OF THE SYSTEM
 *  ===========================================================================
 *
 * (or loses connection to a majority, which is equivalent).  Of course, the
 * cycle of prepares might still fail, in which case we remain in line behind
 * some number of higher-ranked acceptors, and if they all leave or fail, we
 * will at that point begin another cycle of prepares.
 *
 * Any time we discover that we are first in line to be proposer, we initiate
 * a prepare synchronously.  Moreover, we only ever free a prepare if we have
 * connected to a higher-ranked acceptor to whom we defer proposership, or if
 * we have successfully prepared.  Thus, from anywhere except the codepath
 * between detecting our proposership and preparing, we can check whether we
 * are a prepared proposer by simply checking whether pax->prep is NULL.
 */
int
proposer_prepare(struct paxos_acceptor *old_proposer)
{
  int r = 0;
  struct paxos_header hdr;
  struct paxos_yak py;
  struct paxos_acceptor *acc;

  // We always free the prepare before we would have an opportunity to
  // prepare again.
  assert(pax->prep == NULL);

  // If a majority of acceptors are disconnected, we should just give up and
  // quit the session.
  if (pax->live_count < majority()) {
    return paxos_end(pax);  // Always returns 1.
  }

  // Start a new prepare.
  pax->prep = g_malloc0(sizeof(*pax->prep));

  // Set up a new ballot.
  pax->prep->pp_ballot.id = pax->self_id;
  pax->prep->pp_ballot.gen = ++pax->gen_high;

  // Initialize our counters.  Our only initial acceptor is ourselves, and no
  // one initially redirects.
  pax->prep->pp_acks = 1;
  pax->prep->pp_redirects = 0;

  // Cache the current istart.
  pax->prep->pp_istart = pax->istart;

  // If we aren't repreparing (in which case we pass in old_proposer as NULL),
  // we should queue up deferred parts or kills for every dropped acceptor we
  // have in our list.  We force kills for all the acceptors ranked higher
  // than we are in order to provide the invariant described above.  This
  // choice is well-justified: if our prepare was accepted and we actually
  // make those decrees, then a majority of acceptors agreed, at some recent
  // point, that all those higher-ranked acceptors had indeed left the system.
  //
  // It is possible that between now and when we actually send these decrees,
  // one of the acceptors reconnects to us.  If the reconnected acceptor is
  // higher-ranked, we end our prepare.  However, if it is lower-ranked, we
  // do nothing, keeping the part in the defer list.  The part should be
  // rejected by a majority assuming that the acceptor we reconnected to has
  // enough connections in support of its continued existence in the system.
  if (old_proposer != NULL) {
    LIST_FOREACH(acc, &pax->alist, pa_le) {
      // Only kill or part dropped acceptors; also skip ourselves.
      if (acc->pa_peer != NULL || acc->pa_paxid == pax->self_id) {
        continue;
      }

      // Kill higher-ranked acceptors; part lower-ranked ones.
      ERR_ACCUM(r, proposer_decree_part(acc, acc->pa_paxid < pax->self_id));
    }
  }

  // Initialize a Paxos header.
  hdr.ph_session = pax->session_id;
  hdr.ph_ballot.id = pax->prep->pp_ballot.id;
  hdr.ph_ballot.gen = pax->prep->pp_ballot.gen;
  hdr.ph_opcode = OP_PREPARE;
  hdr.ph_inum = pax->ihole;

  // Pack and broadcast the prepare.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  ERR_ACCUM(r, paxos_broadcast(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * Helper routine to obtain the instance on ilist with the closest instance
 * number <= inum.  We are passed in an iterator to simulate a continuation.
 */
static struct paxos_instance *
get_instance_glb(struct paxos_instance *it, struct instance_list *ilist,
    paxid_t inum)
{
  struct paxos_instance *prev;

  prev = NULL;
  for (; it != (void *)ilist; it = LIST_NEXT(it, pi_le)) {
    if (it->pi_hdr.ph_inum > inum) {
      break;
    }
    prev = it;
  }

  return prev;
}

/**
 * proposer_ack_promise - Acknowledge an acceptor's promise.
 *
 * We acknowledge the promise by incrementing the number of acceptors who
 * have responded to the prepare, and by accounting for the acceptor's
 * votes on those decrees for which we do not have commit information.
 *
 * If we attain a majority of promises, we make decrees for all those
 * instances in which any acceptor voted, as well as null decrees for
 * any holes.  We then end the prepare.
 */
int
proposer_ack_promise(struct paxos_header *hdr, msgpack_object *o)
{
  int r;
  msgpack_object *p, *pend;
  struct paxos_instance *inst, *it;
  paxid_t inum;

  // If we're not preparing but are still the proposer, then our prepare has
  // already succeeded, so just return.
  if (pax->prep == NULL) {
    return 0;
  }

  // If the promise is for some other ballot, just ignore it.
  if (ballot_compare(pax->prep->pp_ballot, hdr->ph_ballot) != 0) {
    return 0;
  }

  // Make sure the payload is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);

  // Initialize loop variables.
  p = o->via.array.ptr;
  pend = o->via.array.ptr + o->via.array.size;
  it = pax->prep->pp_istart;

  // Loop through all the vote information.  Note that we assume the votes
  // are sorted by instance number.
  inst = NULL;
  for (; p != pend; ++p) {
    // Allocate an instance if necessary and unpack into it.  Note that if
    // paxos_instance contained any pointers which required deallocation,
    // we would need to destroy it each iteration.
    if (inst == NULL) {
      inst = g_malloc0(sizeof(*inst));
    }
    paxos_instance_unpack(inst, p);

    // Mark everything uncommitted.  We don't care whether any acceptors
    // have already committed; we can safely ask them to recommit without
    // violating correctness.
    inst->pi_committed = false;
    inst->pi_cached = false;
    inst->pi_learned = false;

    // Get the closest instance with lesser or equal instance number.  After
    // starting Paxos, our instance list is guaranteed to always be nonempty,
    // so this should always return a valid instance (so long as we don't
    // pass an iterator which already has a higher inum).
    it = get_instance_glb(it, &pax->ilist, inst->pi_hdr.ph_inum);
    assert(it != NULL);

    if (it->pi_hdr.ph_inum < inst->pi_hdr.ph_inum) {
      // The closest instance is strictly lower in number, so insert after.
      LIST_INSERT_AFTER(&pax->ilist, it, inst, pi_le);

      // Update pax->istart if we just instantiated our hole.
      if (inst->pi_hdr.ph_inum == pax->ihole) {
        pax->istart = inst;
      }

      // We need to allocate a new scratch instance in the next iteration.
      inst = NULL;
    } else {
      // We found an instance of the same number.  If we have not committed
      // this instance, and if the new instance has a higher ballot number,
      // switch the new one in.
      if (!it->pi_committed &&
          ballot_compare(inst->pi_hdr.ph_ballot, it->pi_hdr.ph_ballot) > 0) {
        // Perform the switch; reuse the old allocation in the next iteration.
        LIST_INSERT_AFTER(&pax->ilist, it, inst, pi_le);
        LIST_REMOVE(&pax->ilist, it, pi_le);
        swap((void **)&inst, (void **)&it);
      }
    }
  }

  // Acknowledge the promise.
  pax->prep->pp_acks++;

  // Return if we don't have a majority of acks; otherwise, end the prepare.
  if (pax->prep->pp_acks < majority()) {
    return 0;
  }

  // Set our ballot to the prepare ballot.
  pax->ballot.id = pax->prep->pp_ballot.id;
  pax->ballot.gen = pax->prep->pp_ballot.gen;

  // For each Paxos instance for which we don't have a commit, send a decree.
  for (it = pax->prep->pp_istart, inum = pax->ihole; ; ++inum) {
    // Get the closest instance with number <= inum.
    it = get_instance_glb(it, &pax->ilist, inum);
    assert(it != NULL);

    // Do not redecree by default.
    inst = NULL;

    if (it->pi_hdr.ph_inum < inum) {
      // If inum is strictly past the last instance number seen by a quorum
      // of the entire Paxos system, we're done.
      if (it == LIST_LAST(&pax->ilist)) {
        break;
      }

      // Nobody in the quorum (including ourselves) has heard of this instance,
      // so make a null decree.
      inst = g_malloc0(sizeof(*inst));

      inst->pi_hdr.ph_inum = inum;

      inst->pi_val.pv_dkind = DEC_NULL;
      inst->pi_val.pv_reqid.id = pax->self_id;
      inst->pi_val.pv_reqid.gen = (++pax->req_id);

      LIST_INSERT_AFTER(&pax->ilist, it, inst, pi_le);

      // Update pax->istart if we just instantiated our hole.
      if (inst->pi_hdr.ph_inum == pax->ihole) {
        pax->istart = inst;
      }
    } else if (!it->pi_committed) {
      // The quorum has seen this instance before, but we have not committed
      // it.  By the first part of ack_promise, the vote we have here is the
      // highest-ballot vote of a majority, so decree it again.
      inst = it;
    }

    if (inst != NULL) {
      // Do initialization common to both above paths.
      header_init(&inst->pi_hdr, OP_DECREE, inst->pi_hdr.ph_inum);
      instance_init_metadata(inst);

      // Pack and broadcast the decree.
      ERR_RET(r, paxos_broadcast_instance(inst));
    }
  }

  // Free the prepare.
  g_free(pax->prep);
  pax->prep = NULL;

  // Decree ALL the deferred things!  This includes decreeing parts for any
  // dropped acceptors, in particular the old proposer.
  LIST_WHILE_FIRST(inst, &pax->idefer) {
    LIST_REMOVE(&pax->idefer, inst, pi_le);
    ERR_RET(r, proposer_decree(inst));
  }

  return 0;
}

/**
 * proposer_decree - Broadcast a decree.
 *
 * This function should be called with a paxos_instance struct that has a
 * well-defined value; however, the remaining fields will be rewritten.
 */
int
proposer_decree(struct paxos_instance *inst)
{
  int r;

  // Update the header.
  header_init(&inst->pi_hdr, OP_DECREE, next_instance());

  // Zero out the metadata and mark one vote.
  instance_init_metadata(inst);

  // Insert into the ilist, updating istart.
  instance_insert_and_upstart(inst);

  // Pack and broadcast the decree.
  ERR_RET(r, paxos_broadcast_instance(inst));

  // Do we constitute a majority ourselves?  If so, commit!
  if (inst->pi_votes >= majority()) {
    return proposer_commit(inst);
  }

  return 0;
}

/**
 * proposer_ack_accept - Acknowledge an acceptor's accept.
 *
 * Just increment the vote count of the appropriate Paxos instance and commit
 * if we have a majority.
 */
int
proposer_ack_accept(struct paxos_header *hdr)
{
  struct paxos_instance *inst;

  // If we successfully prepared, we retain the proposership and the ballot
  // we prepared until we leave the system.  Thus, it should not be possible
  // for the ballot not to match here.
  assert(ballot_compare(hdr->ph_ballot, pax->ballot) == 0);

  // Find the decree of the correct instance and increment the vote count.
  inst = instance_find(&pax->ilist, hdr->ph_inum);
  inst->pi_votes++;

  // Ignore the vote if we've already committed.
  if (inst->pi_committed) {
    return 0;
  }

  // If we have a majority, send a commit message.
  if (inst->pi_votes >= majority()) {
    return proposer_commit(inst);
  }

  return 0;
}

/**
 * proposer_commit - Broadcast a commit message for a given Paxos instance.
 *
 * This should only be called when we receive a majority vote for a decree.
 * We broadcast a commit message and mark the instance committed.
 */
int
proposer_commit(struct paxos_instance *inst)
{
  int r;

  // Modify the instance header.
  inst->pi_hdr.ph_opcode = OP_COMMIT;

  // Pack and broadcast the commit.
  ERR_RET(r, paxos_broadcast_instance(inst));

  // Commit and learn the value ourselves.
  return paxos_commit(inst);
}
