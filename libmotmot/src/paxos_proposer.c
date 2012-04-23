/**
 * paxos_proposer.c - Proposer protocol functions for Paxos.
 */
#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "list.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <glib.h>

static inline void
swap(void **p1, void **p2)
{
  void *tmp = *p1;
  *p1 = *p2;
  *p2 = tmp;
}

extern void ilist_insert(struct paxos_instance *);

/**
 * proposer_prepare - Broadcast a prepare message to all acceptors.
 *
 * The initiation of a prepare sequence is only allowed if we believe
 * ourselves to be the proposer.  Moreover, each proposer needs to make it
 * exactly one time.  Therefore, we call proposer_prepare() when and only
 * when:
 *  - We just lost the connection to the previous proposer.
 *  - We were next in line to be proposer.
 */
int
proposer_prepare()
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // It should not be possible for us to already be in a prepare when we
  // start a prepare.  A prepare ends either by (1) ending successfully,
  // (2) ending unsuccessfully because the other acceptors convince us that
  // we are not the proposer, or (3) failing because we crash.  In the first
  // two cases, we free the prepare before ending it, and in the last case
  // it doesn't matter, obviously.
  assert(pax.prep == NULL);

  // Start a new ballot.
  pax.ballot.id = pax.self_id;
  pax.ballot.gen++;

  // Start a new prepare.
  pax.prep = g_malloc0(sizeof(*pax.prep));
  pax.prep->pp_nacks = 1;

  // Initialize a Paxos header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_PREPARE;
  hdr.ph_inum = pax.ihole;

  // Pack and broadcast the prepare.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
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
  msgpack_object *p, *pend;
  struct paxos_instance *inst, *it;
  paxid_t inum;
  struct paxos_yak py;
  struct paxos_acceptor *acc;

  // If the promise is for some other ballot, just ignore it.
  if (ballot_compare(pax.ballot, hdr->ph_ballot) != 0) {
    return 0;
  }

  // If we're not preparing (e.g., we cancelled our prepare upon receiving
  // a redirect), just return.
  if (pax.prep == NULL) {
    return 0;
  }

  // Make sure the payload is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);

  // Initialize loop variables.
  p = o->via.array.ptr;
  pend = o->via.array.ptr + o->via.array.size;
  it = pax.istart;

  // Loop through all the vote information.  Note that we assume the votes
  // are sorted by instance number.
  for (; p != pend; ++p) {
    // Allocate and unpack a instance.  We don't care whether any of the
    // acceptors have already committed; we can safely ask them to recommit
    // without violating correctness.
    // TODO: Figure out a pretty way to deallocate less.
    inst = g_malloc0(sizeof(*inst));
    paxos_instance_unpack(inst, p);
    inst->pi_votes = 1; // Mark uncommitted.

    // Get the closest instance with lesser or equal instance number.  After
    // starting Paxos, our instance list is guaranteed to always be nonempty,
    // so this should always return a valid instance (so long as we don't
    // pass an iterator which already has a higher inum).
    it = get_instance_glb(it, &pax.ilist, inst->pi_hdr.ph_inum);
    assert(it != (void *)&pax.ilist);

    if (it->pi_hdr.ph_inum < inst->pi_hdr.ph_inum) {
      // The closest instance is strictly lower in number, so insert after.
      LIST_INSERT_AFTER(&pax.ilist, it, inst, pi_le);

      // Update pax.istart if we just instantiated our hole.
      if (inst->pi_hdr.ph_inum == pax.ihole) {
        pax.istart = inst;
      }
    } else {
      // We found an instance of the same number.
      if (it->pi_votes != 0 &&
          ballot_compare(inst->pi_hdr.ph_ballot, it->pi_hdr.ph_ballot) > 0) {
        // If the existing instance is NOT a commit, and if the new instance
        // has a higher ballot number, switch the new one in.
        LIST_INSERT_AFTER(&pax.ilist, it, inst, pi_le);
        LIST_REMOVE(&pax.ilist, it, pi_le);
        swap((void **)&inst, (void **)&it);
      }

      // Destroy whichever instance we aren't keeping.
      instance_destroy(inst);
    }
  }

  // Acknowledge the prep.
  pax.prep->pp_nacks++;

  // Return if we don't have a majority of acks; otherwise, end the prepare.
  if (pax.prep->pp_nacks < MAJORITY) {
    return 0;
  }

  // For each Paxos instance for which we don't have a commit, send a decree.
  for (it = pax.istart, inum = pax.ihole; ; ++inum) {
    // Get the closest instance with number <= inum.
    it = get_instance_glb(it, &pax.ilist, inum);
    assert(it != (void *)&pax.ilist);

    if (it->pi_hdr.ph_inum < inum) {
      // If inum is past the last instance number seen by the entire Paxos
      // system, we're done.
      if (it == LIST_LAST(&pax.ilist)) {
        break;
      }

      // Nobody in the quorum (including ourselves) has heard of this instance,
      // so make a null decree.
      inst = g_malloc0(sizeof(*inst));

      inst->pi_hdr.ph_ballot = pax.ballot;
      inst->pi_hdr.ph_opcode = OP_DECREE;
      inst->pi_hdr.ph_inum = inum;

      inst->pi_votes = 1;

      inst->pi_val.pv_dkind = DEC_NULL;
      inst->pi_val.pv_reqid.id = pax.self_id;
      inst->pi_val.pv_reqid.gen = pax.req_id;

      LIST_INSERT_AFTER(&pax.ilist, it, inst, pi_le);

      // Update pax.istart if we just instantiated our hole.
      if (inst->pi_hdr.ph_inum == pax.ihole) {
        pax.istart = inst;
      }
    } else if (it->pi_votes != 0) {
      // The quorum has seen this instance before, but it has not been
      // committed.  By the first part of ack_promise, the vote we have here
      // is the highest-ballot vote, so decree it again.
      inst = it;
      inst->pi_hdr.ph_ballot = pax.ballot;
      inst->pi_hdr.ph_opcode = OP_DECREE;
      inst->pi_votes = 1;
    }

    // Pack and broadcast the decree.
    if (inst != NULL) {
      paxos_payload_init(&py, 2);
      paxos_header_pack(&py, &(inst->pi_hdr));
      paxos_value_pack(&py, &(inst->pi_val));
      paxos_broadcast(UNYAK(&py));
      paxos_payload_destroy(&py);
    }
  }

  // Free the prepare.
  g_free(pax.prep);
  pax.prep = NULL;

  // Forceably PART any dropped acceptors we have in our acceptor list, making
  // sure to skip ourselves since we have no pa_peer.
  LIST_FOREACH(acc, &pax.alist, pa_le) {
    if (acc->pa_peer == NULL && acc->pa_paxid != pax.self_id) {
      // Initialize a new instance.
      inst = g_malloc0(sizeof(*inst));
      inst->pi_val.pv_dkind = DEC_PART;
      inst->pi_val.pv_reqid.id = pax.self_id;
      inst->pi_val.pv_reqid.gen = pax.req_id;
      inst->pi_val.pv_extra = acc->pa_paxid;

      // Decree it.
      proposer_decree(inst);
    }
  }

  // Decree ALL the deferred things!
  inst = NULL;
  LIST_FOREACH(it, &pax.idefer, pi_le) {
    if (inst != NULL) {
      LIST_REMOVE(&pax.idefer, inst, pi_le);
      proposer_decree(inst);
    }
    inst = it;
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
  struct paxos_yak py;

  // Update the header.
  inst->pi_hdr.ph_ballot.id = pax.ballot.id;
  inst->pi_hdr.ph_ballot.gen = pax.ballot.gen;
  inst->pi_hdr.ph_opcode = OP_DECREE;
  inst->pi_hdr.ph_inum = next_instance();

  // Insert into the ilist, updating pi_votes and pax.istart.
  ilist_insert(inst);

  // Pack and broadcast the decree.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_value_pack(&py, &(inst->pi_val));
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  // Do we constitute a majority ourselves?  If so, commit!
  if (inst->pi_votes >= MAJORITY) {
    return proposer_commit(inst);
  }

  return 0;
}

/**
 * proposer_ack_accept - Acknowledge an acceptor's accept.
 *
 * Just increment the vote count of the correct Paxos instance and commit
 * if we have a majority.
 */
int
proposer_ack_accept(struct paxos_peer *source, struct paxos_header *hdr)
{
  struct paxos_instance *inst;

  // We never change the ballot after our initial prepare, and in particular,
  // the ballot cannot refer to some earlier ballot also prepared by us.
  // Thus, it should not be possible for the ballot not to match.
  assert(ballot_compare(hdr->ph_ballot, pax.ballot) == 0);

  // Find the decree of the correct instance and increment the vote count.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  inst->pi_votes++;

  // If we have a majority, send a commit message.
  if (inst->pi_votes >= MAJORITY) {
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
  struct paxos_yak py;

  // Modify the instance header.
  inst->pi_hdr.ph_opcode = OP_COMMIT;

  // Pack and broadcast the commit.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_value_pack(&py, &(inst->pi_val));
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  // Commit and learn the value ourselves.
  return paxos_commit(inst);
}
