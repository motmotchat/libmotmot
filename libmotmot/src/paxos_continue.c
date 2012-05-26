/**
 * paxos_continue.c - Continuation-like callbacks which are invoked by the
 * client once GIOChannel connections are established.
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

#define CONNECTINUATE(op)                                       \
  int                                                           \
  continue_##op(GIOChannel *chan, void *data)                   \
  {                                                             \
    int r = 0;                                                  \
    struct paxos_continuation *k;                               \
    struct paxos_acceptor *acc;                                 \
                                                                \
    k = data;                                                   \
    pax = session_find(&state.sessions, k->pk_session_id);      \
    if (pax == NULL) {                                          \
      return 0;                                                 \
    }                                                           \
                                                                \
    /* Obtain the acceptor.  Only do the continue if the  */    \
    /* acceptor has not been parted in the meantime.      */    \
    acc = acceptor_find(&pax->alist, k->pk_paxid);              \
    if (acc != NULL) {                                          \
      r = do_continue_##op(chan, acc, k);                       \
    }                                                           \
                                                                \
    LIST_REMOVE(&pax->clist, k, pk_le);                         \
    continuation_destroy(k);                                    \
                                                                \
    return r;                                                   \
  }

/**
 * continue_welcome - Register our connection to the new acceptor, decreeing
 * a part if connection failed.
 */
int
do_continue_welcome(GIOChannel *chan, struct paxos_acceptor *acc,
    struct paxos_continuation *k)
{
  int r;
  struct paxos_header hdr;
  struct paxos_acceptor *acc_it;
  struct paxos_instance *inst_it;
  struct paxos_yak py;

  acc->pa_peer = paxos_peer_init(chan);
  if (acc->pa_peer != NULL) {
    pax->live_count++;
  } else {
    return proposer_decree_part(acc, 0);
  }

  // Initialize a header.  The new acceptor's ID is also the instance number
  // of its JOIN.
  header_init(&hdr, OP_WELCOME, acc->pa_paxid);

  // Pack the header into a new payload.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);
  paxos_payload_begin_array(&py, 3);

  // Start off the info payload with the session ID and ibase.
  paxos_payload_begin_array(&py, 2);
  paxos_uuid_pack(&py, pax->session_id);
  paxos_paxid_pack(&py, pax->ibase);

  // Pack the entire alist.  Hopefully we don't have too many un-parted
  // dropped acceptors (we shouldn't).
  paxos_payload_begin_array(&py, LIST_COUNT(&pax->alist));
  LIST_FOREACH(acc_it, &pax->alist, pa_le) {
    paxos_acceptor_pack(&py, acc_it);
  }

  // Pack the entire ilist.
  paxos_payload_begin_array(&py, LIST_COUNT(&pax->ilist));
  LIST_FOREACH(inst_it, &pax->ilist, pi_le) {
    paxos_instance_pack(&py, inst_it);
  }

  // Send the welcome.
  r = paxos_send(acc, UNYAK(&py));
  paxos_payload_destroy(&py);

  return r;
}
CONNECTINUATE(welcome);

/**
 * continue_ack_welcome - Register our initial connections with the other
 * acceptors.
 */
int
do_continue_ack_welcome(GIOChannel *chan, struct paxos_acceptor *acc,
    struct paxos_continuation *k)
{
  int r;

  acc->pa_peer = paxos_peer_init(chan);
  if (acc->pa_peer != NULL) {
    pax->live_count++;
    ERR_RET(r, paxos_hello(acc));
  }

  return 0;
}
CONNECTINUATE(ack_welcome);

/**
 * continue_redirect - If we were able to reestablish connection with the
 * purported proposer, relinquish our proposership, clear our defer list,
 * and reintroduce ourselves.  Otherwise, try preparing again.
 */
int
do_continue_ack_redirect(GIOChannel *chan, struct paxos_acceptor *acc,
    struct paxos_continuation *k)
{
  // Sanity check the choice of acc.
  assert(acc->pa_paxid > pax->self_id);

  // If connection to the acceptor has already been reestablished, we should
  // no longer be the proposer and we can simply return.
  if (acc->pa_peer == NULL) {
    assert(!is_proposer());
    return 0;
  }

  // Register the reconnection; on failure, just reprepare.
  acc->pa_peer = paxos_peer_init(chan);
  if (acc->pa_peer != NULL) {
    // Account for a new acceptor.
    pax->live_count++;

    // We update the proposer only if we have not, since we began reconnecting
    // to acc, reconnected to an even higher-ranked acceptor.
    if (pax->proposer->pa_paxid > acc->pa_paxid) {
      pax->proposer = acc;
    }

    // Free the prepare and destroy our defer list.
    g_free(pax->prep);
    pax->prep = NULL;
    instance_list_destroy(&pax->idefer);

    // Say hello.
    return paxos_hello(acc);
  } else {
    return proposer_prepare(NULL);
  }
}
CONNECTINUATE(ack_redirect);

/**
 * continue_refuse - If we were able to reestablish connection with the
 * purported proposer, reset our proposer and reintroduce ourselves.
 */
int
do_continue_ack_refuse(GIOChannel *chan, struct paxos_acceptor *acc,
    struct paxos_continuation *k)
{
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

    // We update the proposer only if we have not, since we began reconnecting
    // to acc, reconnected to an even higher-ranked acceptor.
    if (pax->proposer->pa_paxid > acc->pa_paxid) {
      pax->proposer = acc;
    }

    // Free any prep we have.  Although, when we acknowledge a refuse, we
    // dispatch as an acceptor, when the ack continues, we may have become
    // the proposer.  If we were preparing, we should give up.
    // XXX: Think some more about whether this is the right choice.
    g_free(pax->prep);
    pax->prep = NULL;
    instance_list_destroy(&pax->idefer);

    // XXX: Resend our request.

    // Say hello.
    return paxos_hello(acc);
  } else {
    return 0;
  }
}
CONNECTINUATE(ack_refuse);

/**
 * continue_reject - If we were able to reestablish connection, reintroduce
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
  inst = instance_find(&pax->ilist, k->pk_inum);
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
