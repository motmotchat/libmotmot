/**
 * paxos_greet.c - Participant initialization protocol for Paxos.
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

/**
 * proposer_welcome - Welcome a new protocol participant by passing along
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
 * We also initiate the connection to the new acceptor, but we assume that
 * the rest of the acceptor object has been initialized already.
 */
int
proposer_welcome(struct paxos_acceptor *acc)
{
  int r;
  struct paxos_connectinue *conn;

  // Initiate a connection with the new acceptor.
  conn = connectinue_new(continue_welcome, acc->pa_paxid);
  ERR_RET(r, state.connect(acc->pa_desc, acc->pa_size, &conn->pc_cb));

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
  int r;
  pax_uuid_t *uuid;
  msgpack_object *arr, *p, *pend;
  struct paxos_acceptor *acc;
  struct paxos_instance *inst;
  struct paxos_connectinue *conn;

  // Create a new session.
  pax = session_new(NULL, 0);
  pax->client_data = state.enter(pax);

  // Set our local state.
  pax->ballot.id = hdr->ph_ballot.id;
  pax->ballot.gen = hdr->ph_ballot.gen;
  pax->self_id = hdr->ph_inum;
  pax->gen_high = hdr->ph_ballot.gen;

  // Make sure the payload is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 3);
  arr = o->via.array.ptr;

  // Unpack the session ID and ibase.
  assert(arr->type == MSGPACK_OBJECT_ARRAY);
  p = (arr++)->via.array.ptr;

  paxos_uuid_unpack(&pax->session_id, p++);
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  pax->ibase = (p++)->via.u64;

  // Add a sync for this session.
  uuid = g_malloc0(sizeof(*uuid));
  *uuid = pax->session_id;
  g_timeout_add_seconds(1, paxos_sync, uuid);

  // Make sure the alist is well-formed.
  assert(arr->type == MSGPACK_OBJECT_ARRAY);
  pend = arr->via.array.ptr + arr->via.array.size;
  p = (arr++)->via.array.ptr;

  // We are live!
  pax->live_count = 1;

  // Unpack the alist.  For each acceptor, in addition to adding an acceptor
  // object to our list, we make a connection and send a hello message.
  for (; p != pend; ++p) {
    acc = g_malloc0(sizeof(*acc));
    paxos_acceptor_unpack(acc, p);
    LIST_INSERT_TAIL(&pax->alist, acc, pa_le);

    if (acc->pa_paxid == hdr->ph_ballot.id) {
      // Don't send a hello to the proposer.
      pax->proposer = acc;
      pax->proposer->pa_peer = source;
      pax->live_count++;
    } else if (acc->pa_paxid != pax->self_id) {
      // Connect to everyone but ourselves.  When we continue, we will say
      // hello to these acceptors.
      conn = connectinue_new(continue_ack_welcome, acc->pa_paxid);
      ERR_RET(r, state.connect(acc->pa_desc, acc->pa_size, &conn->pc_cb));
    }
  }

  // Make sure the ilist is well-formed.
  assert(arr->type == MSGPACK_OBJECT_ARRAY);
  pend = arr->via.array.ptr + arr->via.array.size;
  p = (arr++)->via.array.ptr;

  // Unpack the ilist.
  for (; p != pend; ++p) {
    inst = g_malloc0(sizeof(*inst));
    paxos_instance_unpack(inst, p);
    LIST_INSERT_TAIL(&pax->ilist, inst, pi_le);
  }

  // Determine our ihole.  The first instance in the ilist should always have
  // the instance number pax->ibase, so we start searching there.
  inst = LIST_FIRST(&pax->ilist);
  pax->ihole = pax->ibase;
  for (;; inst = LIST_NEXT(inst, pi_le), ++pax->ihole) {
    // If we reached the end of the list, set pax->istart to the last existing
    // instance.
    if (inst == (void *)&pax->ilist) {
      pax->istart = LIST_LAST(&pax->ilist);
      break;
    }

    // If we skipped over an instance number because we were missing an
    // instance, set pax->istart to the last instance before the hole.
    if (inst->pi_hdr.ph_inum != pax->ihole) {
      pax->istart = LIST_PREV(inst, pi_le);
      break;
    }

    // If we found an uncommitted instance, set pax->istart to it.
    if (!inst->pi_committed) {
      pax->istart = inst;
      break;
    }

    // For now, we no-op learns of old commits when we join the system.
    inst->pi_cached = 1;
    inst->pi_learned = 1;
  }

  return 0;
}

/**
 * paxos_hello - Let an acceptor know our identity.
 *
 * We may have just joined the system, or we may be reintroducing ourselves
 * after a dropped connection was reestablished.
 */
int
paxos_hello(struct paxos_acceptor *acc)
{
  int r;
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize the header.  We pass our own acceptor ID in ph_inum.
  header_init(&hdr, OP_HELLO, pax->self_id);

  // Pack and send the hello.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  r = paxos_send(acc, UNYAK(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * paxos_ack_hello - Record the identity of a fellow acceptor.
 *
 * We may receive hellos either from new acceptors or from acceptors who are
 * reconnecting to us after our connection was dropped.
 */
int
paxos_ack_hello(struct paxos_peer *source, struct paxos_header *hdr)
{
  struct paxos_acceptor *acc;

  // Grab our acceptor from the list.
  acc = acceptor_find(&pax->alist, hdr->ph_inum);

  // If we have not yet created an acceptor object, then the acceptor is new
  // to the system but we have not yet committed and learned its join.  In
  // this case, we defer registering the hello by creating a new object and
  // inserting to a defer list.  Adding to the main alist now could cause
  // loss of consistency.
  if (acc == NULL) {
    acc = g_malloc0(sizeof(*acc));
    acc->pa_paxid = hdr->ph_inum;
    acc->pa_peer = source;
    acceptor_insert(&pax->adefer, acc);
    return 0;
  }

  if (acc->pa_peer == NULL) {
    // If there is no peer, just attach it.
    acc->pa_peer = source;
    pax->live_count++;
  } else if (hdr->ph_inum < pax->self_id) {
    // If our acceptor already has a peer attached, both we and the acceptor
    // attempted to reconnect concurrently and succeeded.  In this case, we
    // keep the peer created by the higher-ranking (i.e., lower-ID) acceptor,
    // so if our rank is lower, switch in the correct peer.
    paxos_peer_destroy(acc->pa_peer);
    acc->pa_peer = source;
  }

  // Suppose the source of the hello is the proposer.  The proposer only says
  // hello when it attempts to part us, is rejected, and then successfully
  // reestablishes a connection.  We claim that, in this case, we can reset
  // our ballot blindly without fear of inconsistency.  We have three cases
  // to consider:
  //
  // 1. Our ballot numbers are equal.  Resetting is a no-op.
  //
  // 2. Our local ballot number is higher.  This means that someone else who
  // was disconnected from the proposer must have prepared.  However, because
  // the proposer was able to achieve a majority of responses in rejecting the
  // part decree, we know that, despite our local disconnect, the proposer is
  // still supported by a majority, and hence the prepare for which we updated
  // our ballot will fail.
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
  if (hdr->ph_inum == pax->proposer->pa_paxid) {
    pax->ballot.id = hdr->ph_ballot.id;
    pax->ballot.gen = hdr->ph_ballot.gen;
  }

  return 0;
}
