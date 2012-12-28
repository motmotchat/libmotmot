/**
 * paxos_retry.c - Protocol for obtaining missing commits.
 */

#include <assert.h>
#include <glib.h>

#include "common/yakyak.h"

#include "paxos.h"
#include "paxos_connect.h"
#include "paxos_protocol.h"
#include "paxos_state.h"
#include "paxos_util.h"
#include "containers/list.h"
#include "util/paxos_io.h"
#include "util/paxos_print.h"

/**
 * acceptor_retry - Ask the proposer to give us a commit we are missing.
 */
int
acceptor_retry(paxid_t hole)
{
  int r;
  struct paxos_header hdr;
  struct yakyak yy;

  // Initialize a header.
  header_init(&hdr, OP_RETRY, hole);

  // Pack and send the payload.
  yakyak_init(&yy, 1);
  paxos_header_pack(&yy, &hdr);
  r = paxos_send_to_proposer(&yy);
  yakyak_destroy(&yy);

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
  inst = instance_find(&pax->ilist, hdr->ph_inum);
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
  struct yakyak yy;

  // Modify the header.
  hdr->ph_opcode = OP_RECOMMIT;

  // Pack and send the recommit.
  yakyak_init(&yy, 2);
  paxos_header_pack(&yy, hdr);
  paxos_value_pack(&yy, &(inst->pi_val));
  r = paxos_broadcast(&yy);
  yakyak_destroy(&yy);

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
  inst = instance_find(&pax->ilist, hdr->ph_inum);
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
