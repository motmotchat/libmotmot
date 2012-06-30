/**
 * paxos_acceptor.c - Acceptor protocol functions for Paxos.
 */

#include <assert.h>
#include <glib.h>

#include "paxos.h"
#include "paxos_continue.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "paxos_state.h"
#include "paxos_util.h"
#include "containers/list.h"

/**
 * acceptor_ack_prepare - Prepare for a new proposer.
 *
 * If we agree that the preparer should be the proposer, respond accordingly;
 * otherwise, redirect the preparer to the individual we believe is most
 * eligible to propose.
 */
int
acceptor_ack_prepare(struct paxos_peer *source, struct paxos_header *hdr)
{
  // If the ballot being prepared for is <= our most recent ballot, or if
  // the preparer is not the highest-ranking acceptor (i.e., the proposer),
  // send a redirect.
  if (ballot_compare(hdr->ph_ballot, pax->ballot) <= 0 ||
      pax->proposer->pa_peer != source) {
    return acceptor_redirect(source, hdr);
  }

  return acceptor_promise(hdr);
}

/**
 * acceptor_promise - Promise fealty to our new overlord.
 *
 * Send the proposer a promise to accept their decrees in perpetuity.  We
 * also send them a list of all of the accepts we have for those instances
 * which the new proposer doesn't know about.  We pack entire instances
 * and send them over the wire for convenience.
 */
int
acceptor_promise(struct paxos_header *hdr)
{
  int r;
  size_t count;
  struct paxos_instance *it;
  struct paxos_yak py;

  // Set our ballot to the one given in the prepare.
  pax->ballot.id = hdr->ph_ballot.id;
  pax->ballot.gen = hdr->ph_ballot.gen;
  pax->gen_high = pax->ballot.gen;

  // Start off the payload with the header.
  hdr->ph_opcode = OP_PROMISE;
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);

  count = 0;

  // Determine how many accepts we need to send back.
  LIST_FOREACH_REV(it, &pax->ilist, pi_le) {
    count++;
    if (hdr->ph_inum >= it->pi_hdr.ph_inum) {
      break;
    }
  }

  // Start the payload of promises.
  paxos_payload_begin_array(&py, count);

  // Pack all the instances starting at the lowest-numbered instance requested.
  for (; it != (void *)&pax->ilist; it = LIST_NEXT(it, pi_le)) {
    paxos_instance_pack(&py, it);
  }

  // Send off our payload.
  r = paxos_send_to_proposer(&py);
  paxos_payload_destroy(&py);

  return r;
}

/**
 * acceptor_ack_decree - Accept a value for a Paxos instance.
 *
 * Move to commit the given value for the given Paxos instance.  If the decree
 * is a part and we believe that the target of the part is still live, we may
 * also reject.
 */
int
acceptor_ack_decree(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_value val;
  struct paxos_acceptor *acc;
  struct paxos_instance *inst;

  // Check the ballot on the message.  If it's not the most recent ballot
  // that we've prepared for, we do not agree with the decree and simply take
  // no action.
  //
  // We do this because the proposer doesn't keep track of who has responded
  // to prepares.  Hence, if we respond to a decree of the decreer's higher
  // ballot, we could break correctness guarantees if we responded later to
  // a decree with our lower local ballot number.
  if (ballot_compare(hdr->ph_ballot, pax->ballot) != 0) {
    return 0;
  }

  // Our local notion of the ballot should match our notion of the proposer.
  // Only when failover has just occurred but we have not yet received the new
  // proposer's prepare is this not the case.
  assert(pax->proposer->pa_paxid == pax->ballot.id);

  // Unpack the value and see if it decrees a part.  If so, but if the target
  // acceptor is still alive, reject the decree.
  paxos_value_unpack(&val, o);
  if (val.pv_dkind == DEC_PART) {
    acc = acceptor_find(&pax->alist, val.pv_extra);
    if (acc->pa_peer != NULL) {
      return acceptor_reject(hdr);
    }
  }

  // See if we have seen this instance for another ballot.
  inst = instance_find(&pax->ilist, hdr->ph_inum);
  if (inst == NULL) {
    // We haven't seen this instance, so initialize a new one.  Our commit
    // flags are all zeroed so we don't need to initialize them.
    inst = g_malloc0(sizeof(*inst));
    memcpy(&inst->pi_hdr, hdr, sizeof(*hdr));
    memcpy(&inst->pi_val, &val, sizeof(val));

    // Insert into the ilist and update istart.
    instance_insert_and_upstart(inst);

    // Accept the decree.
    return acceptor_accept(hdr);
  } else {
    // We found an instance of the same number.
    if (inst->pi_committed) {
      // If we have already committed, assert that the new value matches,
      // then just accept.
      assert(inst->pi_val.pv_dkind == val.pv_dkind);
      assert(inst->pi_val.pv_reqid.id == val.pv_reqid.id);
      assert(inst->pi_val.pv_reqid.gen == val.pv_reqid.gen);

      return acceptor_accept(hdr);
    } else if (ballot_compare(hdr->ph_ballot, inst->pi_hdr.ph_ballot) >= 0) {
      // Otherwise, if the decree has a ballot number equal to or higher than
      // that of our instance, switch the new value in and accept.
      memcpy(&inst->pi_hdr, hdr, sizeof(*hdr));
      memcpy(&inst->pi_val, &val, sizeof(val));

      return acceptor_accept(hdr);
    }
  }

  return 0;
}

/**
 * acceptor_accept - Notify the proposer that we accept their decree.
 */
int
acceptor_accept(struct paxos_header *hdr)
{
  int r;
  struct paxos_yak py;

  // Pack a header.
  hdr->ph_opcode = OP_ACCEPT;
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, hdr);

  // Send the payload.
  r = paxos_send_to_proposer(&py);
  paxos_payload_destroy(&py);

  return r;
}

/**
 * acceptor_ack_commit - Commit a value.
 *
 * Note that we don't check the ballot of the commit; if a commit is made, it
 * is guaranteed by Paxos to be consistent, and hence we can blindly accept
 * it.  We also call paxos_learn() to notify listeners of the value payload.
 */
int
acceptor_ack_commit(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_instance *inst;

  // Retrieve the instance struct corresponding to the inum.
  inst = instance_find(&pax->ilist, hdr->ph_inum);

  // The instance may not exist if we didn't get the original decree, but
  // we can trust the majority and commit anyway.  Our commit flags are
  // all zeroed so we don't need to initialize them.
  if (inst == NULL) {
    inst = g_malloc0(sizeof(*inst));
    memcpy(&inst->pi_hdr, hdr, sizeof(*hdr));
    instance_insert_and_upstart(inst);
  }

  // If we committed already, we know that a quorum has the same value that
  // we do, which means any other decree for the same instance will share
  // this same value.  So just return.
  if (inst->pi_committed) {
    return 0;
  }

  // It's possible that we accepted a decree for inst->pi_inum which was never
  // committed, and then we received a commit for a later ballot for which
  // we never received the original decree.  So, we always reset the value.
  paxos_value_unpack(&inst->pi_val, o);

  // Perform the commit.
  return paxos_commit(inst);
}
