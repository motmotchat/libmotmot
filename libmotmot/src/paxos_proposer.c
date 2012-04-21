/**
 * paxos_proposer.c - Proposer protocol functions for Paxos.
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
  pax.prep->pp_first = NULL;

  // Obtain the first hole.
  pax.prep->pp_hole = ilist_first_hole(&pax.prep->pp_first, &pax.ilist,
                                       pax.ibase);

  // Initialize a Paxos header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_PREPARE;
  hdr.ph_inum = pax.prep->pp_hole;

  // Pack and broadcast the prepare.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
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

  // If the promise is for some other ballot, just ignore it.  Acceptors
  // should only be sending a promise to us in response to a prepare from
  // us.  There is no reason to redirect because our prepare should arrive
  // before any message we send now.
  if (!ballot_compare(pax.ballot, hdr->ph_ballot)) {
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
  it = pax.prep->pp_first;

  // Allocate a scratch instance.
  inst = g_malloc0(sizeof(*inst));

  // Loop through all the vote information.  Note that we assume the votes
  // are sorted by instance number.
  for (; p != pend; ++p) {
    // Unpack a instance.
    paxos_instance_unpack(inst, p);
    inst->pi_votes = 1;

    // Get the closest instance with instance number >= the instance number
    // of inst.
    it = get_instance_lub(it, &pax.ilist, inst->pi_hdr.ph_inum);

    if (it == (void *)&pax.ilist) {
      // We didn't find an instance, so insert at the tail.
      LIST_INSERT_TAIL(&pax.ilist, inst, pi_le);
    } else if (it->pi_hdr.ph_inum > inst->pi_hdr.ph_inum) {
      // We found an instance with a higher number, so insert before it.
      // XXX: We probably want to recommit.
      LIST_INSERT_BEFORE(&pax.ilist, it, inst, pi_le);
    } else {
      // We found an instance of the same number.  If the existing instance
      // is NOT a commit, and if the new instance has a higher ballot number,
      // switch the new one in.
      if (it->pi_votes != 0 &&
          ballot_compare(inst->pi_hdr.ph_ballot, it->pi_hdr.ph_ballot) > 0) {
        LIST_INSERT_BEFORE(&pax.ilist, it, inst, pi_le);
        LIST_REMOVE(&pax.ilist, it, pi_le);
        swap((void **)&inst, (void **)&it);
      }
    }
  }

  // Free the scratch instance.
  instance_destroy(inst);

  // Acknowledge the prep.
  pax.prep->pp_nacks++;

  // Return if we don't have a majority of acks; otherwise, end the prepare.
  if (pax.prep->pp_nacks < MAJORITY) {
    return 0;
  }

  it = pax.prep->pp_first;

  // For each Paxos instance for which we don't have a commit, send a decree.
  for (inum = pax.prep->pp_hole; ; ++inum) {
    // Get the closest instance with number >= inum.
    it = get_instance_lub(it, &pax.ilist, inum);

    // If we're at the end of the list, break.
    if (it == (void *)&pax.ilist) {
      break;
    }

    inst = NULL;

    if (it->pi_hdr.ph_inum > inum) {
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

      LIST_INSERT_BEFORE(&pax.ilist, it, inst, pi_le);
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

  // Mark one vote.
  inst->pi_votes = 1;

  // Append to the ilist.
  LIST_INSERT_TAIL(&pax.ilist, inst, pi_le);

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
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  // Mark the instance committed.
  inst->pi_votes = 0;

  // Learn the value, i.e., act on the commit.
  return paxos_learn(inst);
}
