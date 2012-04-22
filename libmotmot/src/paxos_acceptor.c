/**
 * paxos_acceptor.c - Acceptor protocol functions for Paxos.
 */
#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_protocol.h"
#include "list.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <glib.h>

/**
 * acceptor_ack_prepare - Prepare for a new proposer.
 *
 * First, we check to see if we think that there's someone else who's more
 * eligible to be proposer.  If there exists such a person, redirect this
 * candidate to that person.
 *
 * If we think that this person would be a good proposer, prepare for their
 * presidency by sending them a list of our accepts for all instance
 * numbers we have seen.
 */
int
acceptor_ack_prepare(struct paxos_peer *source, struct paxos_header *hdr)
{
  // If the ballot being prepared for is <= our most recent ballot, or if
  // the preparer is not who we believe the proposer to be, redirect.
  if (ballot_compare(hdr->ph_ballot, pax.ballot) <= 0 ||
      pax.proposer->pa_peer != source) {
    return paxos_redirect(source, hdr);
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
  size_t count;
  struct paxos_instance *it;
  struct paxos_yak py;

  // Set our ballot to the one given in the prepare.
  pax.ballot.id = hdr->ph_ballot.id;
  pax.ballot.gen = hdr->ph_ballot.gen;

  // Start off the payload with the header.
  hdr->ph_opcode = OP_PROMISE;
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);

  count = 0;

  // Determine how many accepts we need to send back.
  LIST_FOREACH_REV(it, &pax.ilist, pi_le) {
    count++;
    if (hdr->ph_inum >= it->pi_hdr.ph_inum) {
      break;
    }
  }

  // Start the payload of promises.
  paxos_payload_begin_array(&py, count);

  // Pack all the instances starting at the lowest-numbered instance requested.
  for (; it != (void *)&pax.ilist; it = LIST_NEXT(it, pi_le)) {
    paxos_instance_pack(&py, it);
  }

  // Send off our payload.
  paxos_send_to_proposer(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * acceptor_ack_decree - Accept a value for a Paxos instance.
 *
 * Move to commit the given value for the given Paxos instance.
 */
int
acceptor_ack_decree(struct paxos_peer *source, struct paxos_header *hdr,
    msgpack_object *o)
{
  int cmp;
  struct paxos_instance *inst;

  // Check the ballot on the message.
  cmp = ballot_compare(hdr->ph_ballot, pax.ballot);
  if (cmp < 0) {
    // Ignore.
    return 0;
  } else if (cmp > 0) {
    // XXX: What if the decree has a higher ballot?
  }

  // See if we have seen this instance for another ballot.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  if (inst == NULL) {
    // We haven't seen this instance, so initialize a new one.
    inst = g_malloc0(sizeof(*inst));
    memcpy(&inst->pi_hdr, hdr, sizeof(*hdr));
    paxos_value_unpack(&inst->pi_val, o);
    inst->pi_votes = 1; // For acceptors, != 0 just means not committed.

    // Insert the new instance into the ilist.
    inst = instance_insert(&pax.ilist, inst);

    // Update pax.istart if we just instantiated the hole.
    if (inst->pi_hdr.ph_inum == pax.ihole) {
      pax.istart = inst;
    }

    // Accept the decree.
    return acceptor_accept(hdr);
  } else {
    // We found an instance of the same number.  If the existing instance
    // is NOT a commit, and if the new instance has a higher ballot number,
    // switch the new one in.
    if (inst->pi_votes != 0 &&
        ballot_compare(hdr->ph_ballot, inst->pi_hdr.ph_ballot) > 0) {
      memcpy(&inst->pi_hdr, hdr, sizeof(*hdr));
      paxos_value_unpack(&inst->pi_val, o);

      // Accept the decree.
      return acceptor_accept(hdr);
    }
  }

  return 0;
}

/**
 * acceptor_accept - Notify the proposer we accept their decree.
 */
int
acceptor_accept(struct paxos_header *hdr)
{
  struct paxos_yak py;

  // Pack a header.
  hdr->ph_opcode = OP_ACCEPT;
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, hdr);

  // Send the payload.
  paxos_send_to_proposer(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}


/**
 * acceptor_ack_commit - Commit a value.
 *
 * Commit this value as a permanent learned value, and notify listeners of the
 * value payload.
 */
int
acceptor_ack_commit(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_instance *inst;

  // Retrieve the instance struct corresponding to the inum.
  inst = instance_find(&pax.ilist, hdr->ph_inum);

  // XXX: I don't think we need to check that the ballot numbers match
  // because Paxos is supposed to guarantee that a commit command from the
  // proposer will always be consistent.  For the same reason, we shouldn't
  // have to check that inst might be NULL.

  // Perform the commit.
  return paxos_commit(inst);
}
