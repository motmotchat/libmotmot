/**
 * paxos_reject.c - Protocol for rejecting parts.
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
  struct paxos_yak py;

  assert(ballot_compare(hdr->ph_ballot, pax.ballot) == 0);

  // Find the decree of the correct instance and increment the vote count.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  inst->pi_rejects++;

  // If we don't have a majority, just return.
  if (inst->pi_rejects < MAJORITY) {
    return 0;
  }

  // See if we can reconnect to the acceptor we tried to part.
  acc = acceptor_find(&pax.alist, inst->pi_val.pv_extra);
  assert(acc->pa_peer == NULL);
  acc->pa_peer = paxos_peer_init(pax.connect(acc->pa_desc, acc->pa_size));

  if (acc->pa_peer != NULL) {
    // If the reconnect succeeds, nullify inst.
    inst->pi_hdr.ph_opcode = OP_DECREE;
    inst->pi_votes = 1;
    inst->pi_rejects = LIST_COUNT(&pax.alist) - pax.live_count;
    inst->pi_val.pv_dkind = DEC_NULL;
    inst->pi_val.pv_extra = 0;

    // XXX: Greet of some sort?
  }

  // Decree null if the reconnect succeeded, else redecree the part.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_value_pack(&py, &(inst->pi_val));
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

