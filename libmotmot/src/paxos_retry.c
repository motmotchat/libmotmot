/**
 * paxos_retry.c - Protocol for obtaining missing commits.
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

extern void instance_insert_and_upstart(struct paxos_instance *);

/**
 * acceptor_retry - Ask the proposer to give us a commit we are missing.
 */
int
acceptor_retry(paxid_t hole)
{
  int r;
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize a header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_RETRY;
  hdr.ph_inum = hole;

  // Pack and send the payload.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  r = paxos_send_to_proposer(UNYAK(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * proposer_ack_retry - See if we have committed a decree and send it back
 * to an interested acceptor.
 */
int
proposer_ack_retry(struct paxos_header *hdr)
{
  struct paxos_instance *inst;

  // Find the requested instance.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  assert(inst != NULL);

  // Recommit if it's been committed; otherwise, just don't respond.
  if (inst->pi_committed) {
    return proposer_recommit(hdr, inst);
  } else {
    return 0;
  }
}

/**
 * proposer_recommit - Resend a commit to an acceptor.
 */
int
proposer_recommit(struct paxos_header *hdr, struct paxos_instance *inst)
{
  int r;
  struct paxos_yak py;

  // Modify the header.
  hdr->ph_opcode = OP_RECOMMIT;

  // Pack and send the recommit.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);
  paxos_value_pack(&py, &(inst->pi_val));
  r = paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * acceptor_ack_recommit - Fill in a missing commit.
 */
int acceptor_ack_recommit(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_instance *inst;

  // Check if we've already committed since we sent the retry.  If we have,
  // just return.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  if (inst != NULL && inst->pi_committed) {
    return 0;
  }

  // Initialize a new instance if necessary.  Our commit flags are all
  // zeroed so we don't need to initialize them.
  if (inst == NULL) {
    inst = g_malloc0(sizeof(*inst));
    memcpy(&inst->pi_hdr, hdr, sizeof(*hdr));
    instance_insert_and_upstart(inst);
  } else {
    memcpy(&inst->pi_hdr, hdr, sizeof(*hdr));
  }

  // Unpack the value.
  paxos_value_unpack(&inst->pi_val, o);

  // Commit it.
  return paxos_commit(inst);
}
