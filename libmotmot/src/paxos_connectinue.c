/**
 * paxos_connectinue.c - Continuation-like callbacks which are invoked by the
 * client once GIOChannel connections are established.
 */
#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "paxos_util.h"
#include "list.h"

#include <assert.h>
#include <glib.h>

#define CONNECTINUATE(op)                                       \
  int                                                           \
  continue_##op(GIOChannel *chan, void *data)                   \
  {                                                             \
    struct paxos_connectinue *conn;                             \
    struct paxos_acceptor *acc;                                 \
                                                                \
    conn = data;                                                \
                                                                \
    /* Obtain the acceptor, returning if it has been parted. */ \
    acc = acceptor_find(&pax->alist, conn->pc_paxid);           \
    connectinue_destroy(conn);                                  \
    if (acc == NULL || acc->pa_peer != NULL) {                  \
      return 0;                                                 \
    }                                                           \
                                                                \
    return do_continue_##op(chan, acc);                         \
  }

/**
 * continue_welcome - Register our connection to the new acceptor, decreeing
 * a part if connection failed.
 */
int
do_continue_welcome(GIOChannel *chan, struct paxos_acceptor *acc)
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
    return proposer_decree_part(acc);
  }

  // Initialize a header.  The new acceptor's ID is also the instance number
  // of its JOIN.
  header_init(&hdr, OP_WELCOME, acc->pa_paxid);

  // Pack the header into a new payload.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);

  // Start off the info payload with the ibase.
  paxos_payload_begin_array(&py, 3);
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
do_continue_ack_welcome(GIOChannel *chan, struct paxos_acceptor *acc)
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
do_continue_ack_redirect(GIOChannel *chan, struct paxos_acceptor *acc)
{
  acc->pa_peer = paxos_peer_init(chan);
  if (acc->pa_peer != NULL) {
    pax->proposer = acc;
    pax->live_count++;
    instance_list_destroy(&pax->idefer);
    return paxos_hello(acc);
  } else {
    return proposer_prepare();
  }
}
CONNECTINUATE(ack_redirect);

/**
 * continue_refuse - If we were able to reestablish connection with the
 * purported proposer, reset our proposer and reintroduce ourselves.
 */
int
do_continue_ack_refuse(GIOChannel *chan, struct paxos_acceptor *acc)
{
  acc->pa_peer = paxos_peer_init(chan);
  if (acc->pa_peer != NULL) {
    // XXX: Do we want to resend our request?
    pax->proposer = acc;
    pax->live_count++;
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
continue_ack_reject(GIOChannel *chan, void *data)
{
  int r;
  struct paxos_connectinue *conn;
  struct paxos_acceptor *acc;
  struct paxos_instance *inst;

  conn = data;

  // Obtain the acceptor, returning if it has been parted.
  acc = acceptor_find(&pax->alist, conn->pc_paxid);
  if (acc == NULL || acc->pa_peer != NULL) {
    connectinue_destroy(conn);
    return 0;
  }

  // Obtain the instance that was rejected.  If it is gone, it must have been
  // sync'd away, so just return.
  inst = instance_find(&pax->ilist, conn->pc_inum);
  connectinue_destroy(conn);
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
