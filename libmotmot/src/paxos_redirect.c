/**
 * paxos_redirect.c - Protocol for telling acceptors they are wrong about
 * something.
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

/**
 * paxos_redirect - Tell the sender of the message either that we are not the
 * proposer or that they are not the proposer, depending on whether or not
 * they think they are the proposer.
 */
int
paxos_redirect(struct paxos_peer *source, struct paxos_header *recv_hdr)
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize a header.  Our recipients should use ph_inum rather than the
  // ballot ID as the ID of the proposer we are suggesting, since, in the
  // case that a proposer recently failed over and we have not yet received
  // their prepare, the preparer and ballot might different.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_REDIRECT;
  hdr.ph_inum = pax.proposer->pa_paxid;

  // Pack a payload, which includes the header we were sent which we believe
  // to be incorrect.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);
  paxos_header_pack(&py, recv_hdr);

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
 * To ensure that this redirect wasn't intended for us at some point in the
 * past before we were the proposer, we can check the opcode of the second
 * paxos_header we are passed.
 */
int
proposer_ack_redirect(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_header orig_hdr;
  struct paxos_acceptor *acc;

  // Sanity check that the supposed true proposer has a lower ID than we do.
  assert(hdr->ph_inum < pax.self_id);

  // Check that this redirect is a response to a prepare.
  paxos_header_unpack(&orig_hdr, o);
  if (orig_hdr.ph_opcode != OP_PREPARE) {
    return 0;
  }

  // Pull out the acceptor struct corresponding to the purported proposer and
  // try to reconnect.  Note that we should have already set the pa_peer of
  // this acceptor to NULL to indicate the lost connection.
  acc = acceptor_find(&pax.alist, hdr->ph_inum);
  assert(acc->pa_peer == NULL);
  acc->pa_peer = paxos_peer_init(pax.connect(acc->pa_desc, acc->pa_size));

  // If the reconnect succeeds, relinquish proposership and cancel any
  // standing prepare.
  if (acc->pa_peer != NULL) {
    pax.proposer = acc;
    // XXX: Is setting the ballot safe?
    pax.ballot.id = hdr->ph_ballot.id;
    pax.ballot.gen = hdr->ph_ballot.gen;
    if (pax.prep != NULL) {
      g_free(pax.prep);
    }
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
 * ttal ordering, receiving a redirect means that there is someone more
 * fitting to be proposer than the person we identified.
 */
int
acceptor_ack_redirect(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_header orig_hdr;
  struct paxos_acceptor *acc;

  // Check whether, since we sent our request, we have found a more suitable
  // proposer.
  if (pax.proposer->pa_paxid <= hdr->ph_inum) {
    return 0;
  }

  // Check that this redirect is a response to a request.
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

  // If the reconnect succeeds, reset our proposer and ballot info correctly.
  if (acc->pa_peer != NULL) {
    pax.proposer = acc;
    // XXX: Is setting the ballot safe?
    pax.ballot.id = hdr->ph_ballot.id;
    pax.ballot.gen = hdr->ph_ballot.gen;
  }

  return 0;
}
