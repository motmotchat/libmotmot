/**
 * paxos_redirect.c - Protocol for telling acceptors they are wrong about
 * something.
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

#define DEATH_ADJUSTED(n) ((n) + (LIST_COUNT(&pax->alist) - pax->live_count))

/**
 * acceptor_redirect - Tell a preparer that they are not the proposer and
 * thus do not have the right to prepare.
 */
int
acceptor_redirect(struct paxos_peer *source, struct paxos_header *orig_hdr)
{
  int r;
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize a header.  Our recipients should use ph_inum rather than the
  // ballot ID as the ID of the proposer we are suggesting, since, it may be
  // the case that the proposer has assumed the proposership but has not yet
  // prepared to us.
  header_init(&hdr, OP_REDIRECT, pax->proposer->pa_paxid);

  // Pack a payload, which includes the header we were sent which we believe
  // to be incorrect.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);
  paxos_header_pack(&py, orig_hdr);

  // Send the payload.
  r = paxos_peer_send(source, paxos_payload_data(&py), paxos_payload_size(&py));
  paxos_payload_destroy(&py);

  return r;
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
 */
int
proposer_ack_redirect(struct paxos_header *hdr, msgpack_object *o)
{
  int r;
  struct paxos_header orig_hdr;
  struct paxos_acceptor *acc;
  struct paxos_continuation *k;

  // We dispatched as the proposer, so we do not need to check again whether
  // we think ourselves to be the proposer.  Instead, just sanity check that
  // the supposed true proposer has a lower ID than we do.  This should
  // always be the case because of the consistency of proposer ranks.
  assert(hdr->ph_inum < pax->self_id);

  // If we are not still preparing, either we succeeded or our prepare was
  // rejected.  In the former case, we should ignore the redirect because
  // we have affirmed our proposership with a majority vote.  In the latter
  // case, if we connected to the true proposer, we would have dispatched
  // as an acceptor; and if we did not successfully connect, we would have
  // sent out another prepare.  Hence, if we are not preparing, our prepare
  // succeeded and hence we should ignore the redirect.
  if (pax->prep == NULL) {
    return 0;
  }

  // Ensure that the redirect is for our current prepare; otherwise ignore.
  paxos_header_unpack(&orig_hdr, o);
  if (ballot_compare(orig_hdr.ph_ballot, pax->prep->pp_ballot) != 0) {
    return 0;
  }

  // Acknowledge the rejection of our prepare.
  pax->prep->pp_redirects++;

  // If we have been redirected by a majority, attempt reconnection.  If a
  // majority redirects, our prepare will never succeed, but we defer freeing
  // it until reconnection occurs.  This provides us with the guarantee (used
  // above) that an acceptor who identifies as the proposer and whose prepare
  // is non-NULL has either successfully prepared or has not yet begun its
  // prepare cycle.
  if (DEATH_ADJUSTED(pax->prep->pp_redirects) >= majority()) {
    // Connect to the higher-ranked acceptor indicated in the most recent
    // redirect message we received (i.e., this one).  It's possible that an
    // even higher-ranked acceptor exists, but we'll find that out when we
    // try to send a request.
    acc = acceptor_find(&pax->alist, hdr->ph_inum);
    assert(acc->pa_peer == NULL);

    // Defer computation until the client performs connection.  If it succeeds,
    // give up the prepare; otherwise, reprepare.
    k = continuation_new(continue_ack_redirect, acc->pa_paxid);
    ERR_RET(r, state.connect(acc->pa_desc, acc->pa_size, &k->pk_cb));
    return 0;
  }

  // If we have heard back from everyone but the acks and redirects are tied,
  // just prepare again.
  if (pax->prep->pp_acks < majority() &&
      DEATH_ADJUSTED(pax->prep->pp_redirects) < majority() &&
      pax->prep->pp_acks + pax->prep->pp_redirects == pax->live_count) {
    g_free(pax->prep);
    pax->prep = NULL;
    return proposer_prepare(NULL);
  }

  return 0;
}

// XXX: We should check for all these cases whether or not connection was
// reestablished, i.e., in a hello.

/**
 * continue_ack_redirect - If we were able to reestablish connection with the
 * purported proposer, relinquish our proposership, clear our defer list,
 * and reintroduce ourselves.  Otherwise, try preparing again.
 */
int
do_continue_ack_redirect(GIOChannel *chan, struct paxos_acceptor *acc,
    struct paxos_continuation *k)
{
  // Sanity check the choice of acc.
  assert(acc->pa_paxid < pax->self_id);

  // If connection to the acceptor has already been reestablished, we should
  // no longer be the proposer and we can simply return.
  if (acc->pa_peer != NULL) {
    assert(!is_proposer());
    return 0;
  }

  // Free the old prepare regardless of whether reconnection succeeded.
  g_free(pax->prep);
  pax->prep = NULL;

  // Register the reconnection; on failure, reprepare.
  acc->pa_peer = paxos_peer_init(chan);
  if (acc->pa_peer != NULL) {
    // Account for a new acceptor.
    pax->live_count++;

    // We update the proposer only if we have not reconnected to an even
    // higher-ranked acceptor.
    if (acc->pa_paxid < pax->proposer->pa_paxid) {
      pax->proposer = acc;
    }

    // Destroy the defer list; we're finished trying to prepare.
    // XXX: Do we want to somehow pass it to the real proposer?  How do we
    // know which requests were made for us?
    instance_container_destroy(&pax->idefer);

    // Say hello.
    return paxos_hello(acc);
  } else {
    // Prepare again, continuing to append to the defer list.
    return proposer_prepare(NULL);
  }
}
CONNECTINUATE(ack_redirect);

/**
 * acceptor_refuse - Tell a requester that they incorrectly identified us
 * at the proposer and we cannot decree their message.
 */
int
acceptor_refuse(struct paxos_peer *source, struct paxos_header *orig_hdr,
    struct paxos_request *req)
{
  int r;
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize a header.  Our recipients should use ph_inum rather than the
  // ballot ID as the ID of the proposer we are suggesting, since, it may be
  // the case that the proposer has assumed the proposership but has not yet
  // prepared to us.
  header_init(&hdr, OP_REFUSE, pax->proposer->pa_paxid);

  // Pack a payload, which includes the header we were sent which we believe
  // to be incorrect and the request ID of the refused request.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);
  paxos_payload_begin_array(&py, 2);
  paxos_header_pack(&py, orig_hdr);
  paxos_value_pack(&py, &req->pr_val);

  // Send the payload.
  r = paxos_peer_send(source, paxos_payload_data(&py), paxos_payload_size(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * acceptor_ack_refuse - Resolve an acceptor's claim that we do not know
 * the true proposer.
 *
 * If we send a request to someone who is not the proposer, but identifying
 * them as the proposer, we will receive a refuse.  Since the correctness
 * of the Paxos protocol guarantees that the acceptor list has a consistent
 * total ordering across the system, receiving a refuse means that there is
 * someone more fitting to be proposer than the acceptor we identified.
 *
 * Note, as with ack_redirect, that it is possible we noticed a proposer
 * failure and sent our request to the new proposer correctly before the new
 * proposer themselves recognized the failure.
 */
int
acceptor_ack_refuse(struct paxos_header *hdr, msgpack_object *o)
{
  int r;
  msgpack_object *p;
  struct paxos_acceptor *acc;
  struct paxos_continuation *k;

  // Check whether, since we sent our request, we have already found a more
  // suitable proposer, possibly due to another redirect, in which case we
  // can ignore this one.
  if (pax->proposer->pa_paxid <= hdr->ph_inum) {
    return 0;
  }

  // Pull out the acceptor struct corresponding to the purported proposer and
  // try to reconnect.  Note that we should have already set the pa_peer of
  // this acceptor to NULL to indicate the lost connection.
  acc = acceptor_find(&pax->alist, hdr->ph_inum);
  assert(acc->pa_peer == NULL);

  // Defer computation until the client performs connection.  If it succeeds,
  // resend the request.  We bind the request ID as callback data.
  k = continuation_new(continue_ack_refuse, acc->pa_paxid);

  assert(o->type == MSGPACK_OBJECT_ARRAY);
  p = o->via.array.ptr + 1;
  paxos_value_unpack(&k->pk_data.req.pr_val, p++);

  ERR_RET(r, state.connect(acc->pa_desc, acc->pa_size, &k->pk_cb));
  return 0;
}

// XXX: We should check for all these cases whether or not connection was
// reestablished, i.e., in a hello.

/**
 * continue_ack_refuse - If we were able to reestablish connection with the
 * purported proposer, reset our proposer and reintroduce ourselves.
 */
int
do_continue_ack_refuse(GIOChannel *chan, struct paxos_acceptor *acc,
    struct paxos_continuation *k)
{
  int r = 0;
  struct paxos_header hdr;
  struct paxos_request *req;
  struct paxos_yak py;

  // If we are the proposer and have finished preparing, anyone higher-ranked
  // than we are is dead to us.  However, their parts may not yet have gone
  // through, so we make sure to ignore attempts at reconnection.
  if (is_proposer() && pax->prep == NULL) {
    return 0;
  }

  // Register the reconnection.
  acc->pa_peer = paxos_peer_init(chan);
  if (acc->pa_peer != NULL) {
    // Account for a new acceptor.
    pax->live_count++;

    // Free any prep we have.  Although we dispatch as an acceptor when we
    // acknowledge a refuse, when the acknowledgement continues here, we may
    // have become the proposer.  Thus, if we are preparing, we should just
    // give up.  If the acceptor we are reconnecting to fails, we'll find
    // out about the drop and then reprepare.
    g_free(pax->prep);
    pax->prep = NULL;
    instance_container_destroy(&pax->idefer);

    // Say hello.
    ERR_ACCUM(r, paxos_hello(acc));

    if (acc->pa_paxid < pax->proposer->pa_paxid) {
      // Update the proposer only if we have not reconnected to an even
      // higher-ranked acceptor.
      pax->proposer = acc;

      // Resend our request.
      // XXX: What about the problematic case where A is connected to B, B
      // thinks it's the proposer and accepts A's request, but in fact B is not
      // the proposer and C, the real proposer, gets neither of their requests?
      header_init(&hdr, OP_REQUEST, pax->proposer->pa_paxid);

      req = request_find(&pax->rcache, k->pk_data.req.pr_val.pv_reqid);
      if (req == NULL) {
        req = &k->pk_data.req;
      }

      paxos_payload_init(&py, 2);
      paxos_header_pack(&py, &hdr);
      paxos_request_pack(&py, req);

      ERR_ACCUM(r, paxos_send_to_proposer(&py));
      paxos_payload_destroy(&py);
    }
  }

  return r;
}
CONNECTINUATE(ack_refuse);

/**
 * acceptor_reject - Notify the proposer that we reject their decree.
 */
int
acceptor_reject(struct paxos_header *hdr)
{
  int r;
  struct paxos_yak py;

  // Pack a header.
  hdr->ph_opcode = OP_REJECT;
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, hdr);

  // Send the payload.
  r = paxos_send_to_proposer(&py);
  paxos_payload_destroy(&py);

  return r;
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
  int r;
  struct paxos_instance *inst;
  struct paxos_acceptor *acc;
  struct paxos_continuation *k;

  // Our prepare succeeded, so we have only one possible ballot in our
  // lifetime in the system.
  assert(ballot_compare(hdr->ph_ballot, pax->ballot) == 0);

  // Find the decree of the correct instance and increment the reject count.
  inst = instance_find(&pax->ilist, hdr->ph_inum);
  inst->pi_rejects++;

  // Ignore the vote if we've already committed.
  if (inst->pi_committed) {
    return 0;
  }

  // We only reject parts.  However, we may continue to receive rejects even
  // after a majority rejects, in which case we may have re-decreed null.
  if (inst->pi_val.pv_dkind == DEC_NULL) {
    return 0;
  }
  assert(inst->pi_val.pv_dkind == DEC_PART);

  // If we have been rejected by a majority, attempt reconnection.
  if (DEATH_ADJUSTED(inst->pi_rejects) >= majority()) {
    // See if we can reconnect to the acceptor we tried to part.
    acc = acceptor_find(&pax->alist, inst->pi_val.pv_extra);
    assert(acc->pa_peer == NULL);

    // Defer computation until the client performs connection.  If it succeeds,
    // replace the part decree with a null decree; otherwise, just redecree
    // the part.  We bind the instance number of the decree as callback data.
    k = continuation_new(continue_ack_reject, acc->pa_paxid);
    k->pk_data.inum = inst->pi_hdr.ph_inum;
    ERR_RET(r, state.connect(acc->pa_desc, acc->pa_size, &k->pk_cb));
    return 0;
  }

  // If we have heard back from everyone but the accepts and rejects are tied,
  // just decree the part again.
  if (inst->pi_votes < majority() &&
      DEATH_ADJUSTED(inst->pi_rejects) < majority() &&
      inst->pi_votes + inst->pi_rejects == pax->live_count) {
    return paxos_broadcast_instance(inst);
  }

  return 0;
}

// XXX: We should check for all these cases whether or not connection was
// reestablished, i.e., in a hello.

/**
 * continue_ack_reject - If we were able to reestablish connection, reintroduce
 * ourselves and redecree the attempted part as null.  Otherwise, just try
 * decreeing the part again.
 */
int
do_continue_ack_reject(GIOChannel *chan, struct paxos_acceptor *acc,
    struct paxos_continuation *k)
{
  int r;
  struct paxos_instance *inst;

  // Obtain the rejected instance.  If we can't find it, it must have been
  // sync'd away, so just return.
  inst = instance_find(&pax->ilist, k->pk_data.inum);
  if (inst == NULL) {
    return 0;
  }

  acc->pa_peer = paxos_peer_init(chan);
  if (acc->pa_peer != NULL) {
    // Account for a new live connection.
    pax->live_count++;

    // Reintroduce ourselves to the acceptor.
    ERR_RET(r, paxos_hello(acc));

    // Nullify the instance.
    inst->pi_hdr.ph_opcode = OP_DECREE;
    inst->pi_val.pv_dkind = DEC_NULL;
    inst->pi_val.pv_extra = 0;
  }

  // Reset the instance metadata, marking one vote.
  instance_init_metadata(inst);

  // Decree null if the reconnect succeeded, else redecree the part.
  return paxos_broadcast_instance(inst);
}
CONNECTINUATE(ack_reject);
