/**
 * paxos_greet.c - Participant initialization protocol for Paxos.
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
  paxos_payload_begin_array(&py, 2);
  paxos_paxid_pack(&py, pax.ibase);
  paxos_paxid_pack(&py, pax.ihole);

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

  // Make sure the payload is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 3);
  arr = o->via.array.ptr;

  // Unpack the ibase and ihole.
  assert(arr->type == MSGPACK_OBJECT_ARRAY);
  assert(arr->via.array.size == 2);

  p = arr->via.array.ptr;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  pax.ibase = (p++)->via.u64;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  pax.ihole = (p++)->via.u64;

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

  // Set our istart.
  pax.istart = get_instance_glb(LIST_FIRST(&pax.ilist), &pax.ilist, pax.ihole);

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
  return acceptor_hello(acc);
}

/**
 * acceptor_hello - Let a new acceptor know our identity.
 */
int
acceptor_hello(struct paxos_acceptor *acc)
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
 * acceptor_ack_hello - Record the identity of a fellow acceptor.
 */
int
acceptor_ack_hello(struct paxos_peer *source, struct paxos_header *hdr)
{
  struct paxos_acceptor *acc;

  // Associate the peer to the corresponding acceptor.
  acc = acceptor_find(&pax.alist, hdr->ph_inum);
  acc->pa_peer = source;

  return 0;
}
