/**
 * paxos_greet.c - Participant initialization protocol for Paxos.
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

/**
 * proposer_welcome - Welcome new protocol participant by passing along
 *
 * struct {
 *   paxos_header hdr;
 *   struct {
 *     paxid_t ibase;
 *     paxos_acceptor alist[];
 *     paxos_instance ilist[];
 *   } init_info;
 * }
 *
 * The header contains the ballot information and the new acceptor's paxid
 * in ph_inum (since its inum is just the instance number of its JOIN).
 * We also send over our list of acceptors and instances to start the new
 * acceptor off.
 *
 * We avoid sending over our request cache to reduce strain on the network;
 * the new acceptor can issue retrieves to obtain any necessary requests.
 *
 * We assume that the paxos_acceptor argument has already been fully
 * initialized.
 */
int
proposer_welcome(struct paxos_acceptor *acc)
{
  struct paxos_header hdr;
  struct paxos_acceptor *acc_it;
  struct paxos_instance *inst_it;
  struct paxos_yak py;

  // Initialize a header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_WELCOME;

  // The new acceptor's ID is also the instance number of its JOIN.
  hdr.ph_inum = acc->pa_paxid;

  // Pack the header into a new payload.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);

  // Start off the info payload with the ibase.
  paxos_payload_begin_array(&py, 3);
  paxos_paxid_pack(&py, pax.ibase);

  // Pack the entire alist.  Hopefully we don't have too many un-parted
  // dropped acceptors (we shouldn't).
  paxos_payload_begin_array(&py, LIST_COUNT(&pax.alist));
  LIST_FOREACH(acc_it, &pax.alist, pa_le) {
    paxos_acceptor_pack(&py, acc_it);
  }

  // Pack the entire ilist.
  paxos_payload_begin_array(&py, LIST_COUNT(&pax.ilist));
  LIST_FOREACH(inst_it, &pax.ilist, pi_le) {
    paxos_instance_pack(&py, inst_it);
  }

  // Send the welcome.
  paxos_send(acc, UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * acceptor_ack_welcome - Be welcomed to the Paxos system.
 *
 * This allows us to populate our ballot, alist, and ilist, as well as to
 * learn our assigned paxid.  We populate our request cache on-demand with
 * out-of-band retrieve messages.
 */
int
acceptor_ack_welcome(struct paxos_peer *source, struct paxos_header *hdr,
    msgpack_object *o)
{
  msgpack_object *arr, *p, *pend;
  struct paxos_acceptor *acc;
  struct paxos_instance *inst;

  // Set our local state.
  pax.ballot.id = hdr->ph_ballot.id;
  pax.ballot.gen = hdr->ph_ballot.gen;
  pax.self_id = hdr->ph_inum;
  pax.gen_high = hdr->ph_ballot.gen;

  // Make sure the payload is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 3);
  arr = o->via.array.ptr;

  // Unpack the ibase.
  assert(arr->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  pax.ibase = arr->via.u64;

  // Grab the alist array.
  arr++;

  // Make sure the alist is well-formed...
  assert(arr->type == MSGPACK_OBJECT_ARRAY);
  p = arr->via.array.ptr;
  pend = arr->via.array.ptr + arr->via.array.size;

  // ...and unpack our new alist.
  for (; p != pend; ++p) {
    acc = g_malloc0(sizeof(*acc));
    paxos_acceptor_unpack(acc, p);
    LIST_INSERT_TAIL(&pax.alist, acc, pa_le);

    // Set the proposer correctly.
    if (acc->pa_paxid == hdr->ph_ballot.id) {
      pax.proposer = acc;
      pax.proposer->pa_peer = source;
    }
  }

  // Two acceptors are live to us, the proposer and ourselves.
  pax.live_count = 2;

  // Grab the ilist array.
  arr++;

  // Make sure the ilist is well-formed...
  assert(arr->type == MSGPACK_OBJECT_ARRAY);
  p = arr->via.array.ptr;
  pend = arr->via.array.ptr + arr->via.array.size;

  // ...and unpack our new ilist.
  for (; p != pend; ++p) {
    inst = g_malloc0(sizeof(*inst));
    paxos_instance_unpack(inst, p);
    LIST_INSERT_TAIL(&pax.ilist, inst, pi_le);
  }

  // Determine our ihole.  The first instance in the ilist should always have
  // the instance number pax.ibase, so we start searching there.
  inst = LIST_FIRST(&pax.ilist);
  pax.ihole = pax.ibase;
  for (; ; inst = LIST_NEXT(inst, pi_le), ++pax.ihole) {
    // If we reached the end of the list, set pax.istart to the last existing
    // instance.
    if (inst == (void *)&pax.ilist) {
      pax.istart = LIST_LAST(&pax.ilist);
      break;
    }

    // If we skipped over an instance number because we were missing an
    // instance, set pax.istart to the last instance before the hole.
    if (inst->pi_hdr.ph_inum != pax.ihole) {
      pax.istart = LIST_PREV(inst, pi_le);
      break;
    }

    // If we found an uncommitted instance, set pax.istart to it.
    if (inst->pi_votes != 0) {
      pax.istart = inst;
      break;
    }
  }

  return acceptor_ptmy(pax.proposer);
}

/**
 * acceptor_ptmy - Acknowledge a proposer's welcome.
 */
int
acceptor_ptmy(struct paxos_acceptor *acc)
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize a header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_PTMY;
  hdr.ph_inum = pax.self_id;  // Our ID.

  // Pack it and send it back to our greeter.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  paxos_send(acc, UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * proposer_ack_ptmy - Acknowledge a new acceptor's response to your welcome.
 */
int
proposer_ack_ptmy(struct paxos_header *hdr)
{
  struct paxos_acceptor *acc;

  // Grab our acceptor from the list.
  acc = acceptor_find(&pax.alist, hdr->ph_inum);

  // Tell everyone to greet.
  return proposer_greet(hdr, acc);
}

/**
 * proposer_greet - Issue an order to all acceptors to say hello to a newly
 * added acceptor.
 */
int
proposer_greet(struct paxos_header *hdr, struct paxos_acceptor *acc)
{
  struct paxos_yak py;

  // Modify the header.
  hdr->ph_opcode = OP_GREET;

  // Pack and broadcast the greet command.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, hdr);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * acceptor_ack_greet - Act on a proposer's command to say hello to a newly
 * added acceptor.
 */
int
acceptor_ack_greet(struct paxos_header *hdr)
{
  struct paxos_acceptor *acc;

  // Don't greet anyone older than us.
  if (hdr->ph_inum <= pax.self_id) {
    return 0;
  }

  // Grab our acceptor from the list.
  acc = acceptor_find(&pax.alist, hdr->ph_inum);

  // If we have not yet committed and learned a join for the new acceptor,
  // we defer the hello.  We insert a properly identified acceptor object
  // in the acceptor list to signal this deferral to the join protocol.
  if (acc == NULL) {
    acc = g_malloc0(sizeof(*acc));
    acc->pa_paxid = hdr->ph_inum;
    acceptor_insert(&pax.alist, acc);
    return 0;
  }

  // If the join has occurred, say hello to our new acceptor.
  return paxos_hello(acc);
}

/**
 * paxos_hello - Let an acceptor know our identity
 *
 * This acceptor may have just joined the system, or we may be reintroducing
 * ourselves after a dropped connection was reestablished.
 */
int
paxos_hello(struct paxos_acceptor *acc)
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize the header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_HELLO;
  hdr.ph_inum = pax.self_id;  // Overloaded with our acceptor ID.

  // Pack and send the hello.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  paxos_send(acc, UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * paxos_ack_hello - Record the identity of a fellow acceptor.
 *
 * As with paxos_hello(), we may receive a hello if we have just joined the
 * system, or if someone is reconnecting with us after our connection was
 * dropped.
 */
int
paxos_ack_hello(struct paxos_peer *source, struct paxos_header *hdr)
{
  struct paxos_acceptor *acc;

  // Grab the appropriate acceptor object.
  acc = acceptor_find(&pax.alist, hdr->ph_inum);

  if (acc->pa_peer != NULL && hdr->ph_inum < pax.self_id) {
    // If our acceptor already has a peer attached, both we and the acceptor
    // attempted to reconnect concurrently and succeeded.  In this case, we
    // keep the peer created by the higher-ranking (i.e., lower-ID) acceptor.
    paxos_peer_destroy(acc->pa_peer);
    acc->pa_peer = source;
  } else if (acc->pa_peer == NULL) {
    // If there is no peer, just attach it.
    acc->pa_peer = source;
    pax.live_count++;
  }

  // Suppose the source of the hello is the proposer.  The proposer only says
  // hello when a part was rejected and our connection was reestablished.  We
  // claim that, in this case, we can reset our ballot blindly without fear of
  // inconsistency.  We have three cases to consider:
  //
  // 1. Our ballot numbers are equal.  Resetting is a no-op.
  //
  // 2. Our local ballot number is higher.  Someone else who was disconnected
  // from the proposer must have prepared.  However, because the proposer was
  // able to achieve a majority of responses rejecting the part decree, we
  // know that, despite our local disconnect, the proposer is still supported
  // by a majority, and hence the prepare for which we updated our ballot will
  // fail.
  // XXX: Think about this case some more.
  //
  // 3. Our local ballot number is lower.  The proposer is past the prepare
  // phase, which means that a quorum of those votes which the proposer didn't
  // know about at prepare time has already been processed.  Moreover, the
  // proposer's ballot number will never change again in its lifetime, and
  // all future ballots in the system should be higher, so it is safe to
  // update our ballot.
  //
  // So update our ballot already.
  if (hdr->ph_inum == pax.proposer->pa_paxid) {
    pax.ballot.id = hdr->ph_ballot.id;
    pax.ballot.gen = hdr->ph_ballot.gen;
  }

  return 0;
}
