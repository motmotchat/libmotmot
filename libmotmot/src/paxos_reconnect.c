/**
 * paxos_redirect.c - Protocol for telling acceptors they are wrong about
 * something.
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

extern int paxos_broadcast_ihv(struct paxos_instance *);

/**
 * paxos_redirect - Tell the sender of the message either that we are not the
 * proposer or that they are not the proposer, depending on whether or not
 * they think they are the proposer.
 */
int
paxos_redirect(struct paxos_peer *source, struct paxos_header *orig_hdr)
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize a header.  Our recipients should use ph_inum rather than the
  // ballot ID as the ID of the proposer we are suggesting, since, it may be
  // the case that the proposer has assumed the proposership but has not yet
  // prepared to us.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_REDIRECT;
  hdr.ph_inum = pax.proposer->pa_paxid;

  // Pack a payload, which includes the header we were sent which we believe
  // to be incorrect.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);
  paxos_header_pack(&py, orig_hdr);

  // Send the payload.
  paxos_peer_send(source, UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * proposer_ack_redirect - Resolve an acceptor's claim that we are not the
 * true proposer.
 *
 * If we send a prepare to an acceptor who does not believe us to be the
 * true proposer, the acceptor will respond with a redirect.  Since the
 * correctness of Paxos guarantees that the acceptor list has a consistent
 * total ordering, receiving a redirect means that there is someone more
 * fitting to be proposer who we have lost contact with.
 *
 * Note that this does not necessarily mean that the identified proposer is
 * still live; it is possible that we noticed a proposer failure and then
 * prepared before the acceptor who sent the redirect detected the failure.
 * To avoid this as much as possible, we wait for a majority of redirects
 * before accepting defeat and attempting reconnection to our superior.  If
 * we "win" with a majority completing the prepare, then we drop the former
 * proposer regardless of whether he has some connections still open.
 *
 * To ensure that this redirect wasn't intended for us at some point in the
 * past before we became the proposer, we check the opcode of the second
 * paxos_header we are passed.
 */
int
proposer_ack_redirect(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_header orig_hdr;
  struct paxos_acceptor *acc;

  // We dispatched as the proposer, so we do not need to check again whether
  // we think ourselves to be the proposer.  Instead, just sanity check that
  // the supposed true proposer has a lower ID than we do.  This should
  // always be the case because of the consistency of proposer ranks.
  assert(hdr->ph_inum < pax.self_id);

  // If we are not still preparing, either we succeeded or our prepare was
  // rejected.  In the former case, we should ignore the redirect because
  // we have affirmed our proposership with a majority vote.  In the latter
  // case, if we connected to the true proposer, we would have dispatched
  // as an acceptor; and if we did not successfully connect, we would have
  // sent out another prepare.  Hence, if we are not preparing, our prepare
  // succeeded and hence we should ignore the redirect.
  if (pax.prep == NULL) {
    return 0;
  }

  // Ensure that the redirect is for our current prepare; otherwise ignore.
  paxos_header_unpack(&orig_hdr, o);
  if (orig_hdr.ph_opcode != OP_PREPARE ||
      ballot_compare(orig_hdr.ph_ballot, pax.prep->pp_ballot) != 0) {
    return 0;
  }

  // Acknowledge the rejection of our prepare.
  pax.prep->pp_redirects++;

  // If we have been redirected by a majority, give up on the prepare and
  // attempt reconnection.
  if (DEATH_ADJUSTED(pax.prep->pp_redirects) >= majority()) {
    // Free the prepare.
    g_free(pax.prep);
    pax.prep = NULL;

    // Connect to the higher-ranked acceptor indicated in the most recent
    // redirect message we received (i.e., this one).  It's possible that an
    // even higher-ranked acceptor exists, but we'll find that out when we
    // try to send a request.
    acc = acceptor_find(&pax.alist, hdr->ph_inum);
    assert(acc->pa_peer == NULL);
    acc->pa_peer = paxos_peer_init(pax.connect(acc->pa_desc, acc->pa_size));

    if (acc->pa_peer != NULL) {
      // If the reconnect succeeds, relinquish proposership and reintroduce
      // ourselves to the proposer.
      pax.proposer = acc;
      pax.live_count++;
      return paxos_hello(acc);
    } else {
      // If the reconnect fails, try preparing again.
      return proposer_prepare();
    }
  }

  // If we have heard back from everyone but the acks and redirects are tied,
  // just prepare again.
  if (pax.prep->pp_acks < majority() &&
      DEATH_ADJUSTED(pax.prep->pp_redirects) < majority() &&
      pax.prep->pp_acks + pax.prep->pp_redirects == pax.live_count) {
    g_free(pax.prep);
    pax.prep = NULL;
    return proposer_prepare();
  }

  return 0;
}

/**
 * acceptor_ack_redirect - Resolve an acceptor's (possibly the proposer's)
 * claim that we do not know the true proposer.
 *
 * If we send a request to someone who is not the proposer, but identifying
 * them as the proposer, we will receive a redirect.  Since the correctness
 * of the Paxos protocol guarantees that the acceptor list has a consistent
 * total ordering across the system, receiving a redirect means that there is
 * someone more fitting to be proposer than the acceptor we identified.
 *
 * Note, as with ack_proposer, that it is possible we noticed a proposer
 * failure and sent our request to the new proposer correctly before the new
 * proposer themselves recognized the failure.
 *
 * Also, as with ack_proposer, we check the opcode of the second header
 * to ensure validity.
 */
int
acceptor_ack_redirect(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_header orig_hdr;
  struct paxos_acceptor *acc;

  // Check whether, since we sent our request, we have already found a more
  // suitable proposer, possibly due to another redirect, in which case we
  // can ignore this one.
  if (pax.proposer->pa_paxid <= hdr->ph_inum) {
    return 0;
  }

  // Check that this redirect is a response to a request.
  // XXX: Check that it was a request for the current ballot?
  paxos_header_unpack(&orig_hdr, o);
  if (orig_hdr.ph_opcode != OP_REQUEST) {
    return 0;
  }

  // Pull out the acceptor struct corresponding to the purported proposer and
  // try to reconnect.  Note that we should have already set the pa_peer of
  // this acceptor to NULL to indicate the lost connection.
  acc = acceptor_find(&pax.alist, hdr->ph_inum);
  assert(acc->pa_peer == NULL);
  acc->pa_peer = paxos_peer_init(pax.connect(acc->pa_desc, acc->pa_size));

  if (acc->pa_peer != NULL) {
    // If the reconnect succeeds, reset our proposer and reintroduce ourselves.
    // XXX: Do we want to resend our request?
    pax.proposer = acc;
    pax.live_count++;
    return paxos_hello(acc);
  } else {
    return 0;
  }
}

/**
 * acceptor_reject - Notify the proposer that we reject their decree.
 */
int
acceptor_reject(struct paxos_header *hdr)
{
  struct paxos_yak py;

  // Pack a header.
  hdr->ph_opcode = OP_REJECT;
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, hdr);

  // Send the payload.
  paxos_send_to_proposer(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * proposer_ack_reject - Acknowledge an acceptor's reject.
 *
 * Increment the reject count of the appropriate Paxos instance.  If we have
 * a majority of rejects, try to reconnect to the acceptor we attempted to
 * force part.  If we are successful, re-decree null; otherwise, try the part
 * again.
 */
int
proposer_ack_reject(struct paxos_header *hdr)
{
  struct paxos_instance *inst;
  struct paxos_acceptor *acc;

  // Our prepare succeeded, so we have only one possible ballot in our
  // lifetime in the system.
  assert(ballot_compare(hdr->ph_ballot, pax.ballot) == 0);

  // Find the decree of the correct instance and increment the reject count.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  inst->pi_rejects++;

  // We only reject parts.
  assert(inst->pi_val.pv_dkind == DEC_PART);

  // If we have been rejected by a majority, attempt reconnection.
  if (DEATH_ADJUSTED(inst->pi_rejects) >= majority()) {
    // See if we can reconnect to the acceptor we tried to part.
    acc = acceptor_find(&pax.alist, inst->pi_val.pv_extra);
    assert(acc->pa_peer == NULL);
    acc->pa_peer = paxos_peer_init(pax.connect(acc->pa_desc, acc->pa_size));

    if (acc->pa_peer != NULL) {
      // Account for a new live connection.
      pax.live_count++;

      // Reintroduce ourselves to the acceptor.
      paxos_hello(acc);

      // Nullify the instance.
      inst->pi_hdr.ph_opcode = OP_DECREE;
      inst->pi_votes = 1;
      inst->pi_rejects = 0;
      inst->pi_val.pv_dkind = DEC_NULL;
      inst->pi_val.pv_extra = 0;
    }

    // Decree null if the reconnect succeeded, else redecree the part.
    return paxos_broadcast_ihv(inst);
  }

  // If we have heard back from everyone but the accepts and rejects are tied,
  // just decree the part again.
  if (inst->pi_votes < majority() &&
      DEATH_ADJUSTED(inst->pi_rejects) < majority() &&
      inst->pi_votes + inst->pi_rejects == pax.live_count) {
    return paxos_broadcast_ihv(inst);
  }

  return 0;
}