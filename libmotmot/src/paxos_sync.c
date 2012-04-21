/**
 * paxos_sync.c - Log synchronization for Paxos.
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
 * proposer_sync - Send a sync command to all acceptors.
 *
 * For a sync to succeed, all acceptors need to tell us the location of the
 * first hole in their (hopefully mostly contiguous) list of committed
 * Paxos instances.  We take the minimum of these values and then command
 * everyone to truncate everything before the collective system's first hole.
 */
int
proposer_sync()
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // If we're already syncing, increment the skip counter.
  // XXX: Do something with this, perhaps?
  if (pax.sync != NULL) {
    pax.sync->ps_skips++;
    return 1;
  }

  // Create a new sync.
  pax.sync = g_malloc0(sizeof(*(pax.sync)));
  pax.sync->ps_total = LIST_COUNT(&pax.alist);
  pax.sync->ps_nacks = 1; // Including ourselves.
  pax.sync->ps_skips = 0;

  // Initialize a header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_SYNC;
  hdr.ph_inum = (++pax.sync_id);  // Sync number.

  // Pack and broadcast the sync.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * acceptor_ack_sync - Respond to the sync request of a proposer.
 *
 * We respond by sending our the first hole in our instance list.
 */
int
acceptor_ack_sync(struct paxos_header *hdr)
{
  paxid_t hole;
  struct paxos_instance *inst;
  struct paxos_yak py;

  // Obtain the hole.
  hole = ilist_first_hole(&inst, &pax.ilist, pax.ibase);

  // Pack and send the response.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);
  paxos_paxid_pack(&py, hole);
  paxos_send_to_proposer(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * proposer_ack_sync - Update sync state based on acceptor's reply.
 */
int
proposer_ack_sync(struct paxos_header *hdr, msgpack_object *o)
{
  paxid_t hole;

  // Ignore replies to older sync commands.
  if (hdr->ph_inum != pax.sync_id) {
    return 0;
  }

  // Update our knowledge of the first commit hole.
  paxos_paxid_unpack(&hole, o);
  if (hole < pax.sync->ps_hole) {
    pax.sync->ps_hole = hole;
  }

  // Increment nacks and command a truncate if the sync is over.
  pax.sync->ps_nacks++;
  if (pax.sync->ps_nacks == pax.sync->ps_total) {
    return proposer_truncate(hdr);
  }

  return 0;
}

/**
 * Truncate an ilist up to (but not including) a given inum.
 *
 * We also free all associated requests.
 */
static void
ilist_truncate_prefix(struct instance_list *ilist, paxid_t inum)
{
  struct paxos_instance *it, *prev;
  struct paxos_request *req;

  prev = NULL;
  LIST_FOREACH(it, ilist, pi_le) {
    if (prev != NULL) {
      req = request_find(&pax.rcache, prev->pi_val.pv_reqid);
      LIST_REMOVE(&pax.rcache, req, pr_le);
      request_destroy(req);
      LIST_REMOVE(ilist, prev, pi_le);
      instance_destroy(prev);
    }
    if (it->pi_hdr.ph_inum >= inum) {
      break;
    }
    prev = it;
  }

  if (it == (void *)ilist) {
    req = request_find(&pax.rcache, prev->pi_val.pv_reqid);
    LIST_REMOVE(&pax.rcache, req, pr_le);
    request_destroy(req);
    LIST_REMOVE(ilist, prev, pi_le);
    instance_destroy(prev);
  }
}

/**
 * proposer_truncate - Command all acceptors to drop the contiguous prefix
 * of Paxos instances for which every participant has committed.
 */
int
proposer_truncate(struct paxos_header *hdr)
{
  paxid_t hole;
  struct paxos_instance *inst;
  struct paxos_yak py;

  // Obtain our own first instance hole.
  hole = ilist_first_hole(&inst, &pax.ilist, pax.ibase);
  if (hole < pax.sync->ps_hole) {
    pax.sync->ps_hole = hole;
  }

  // Make this hole our new ibase.
  assert(pax.sync->ps_hole >= pax.ibase);
  pax.ibase = pax.sync->ps_hole;

  // Do the truncate (< pax.ibase).
  ilist_truncate_prefix(&pax.ilist, pax.ibase);

  // Pack and broadcast a truncate command.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);
  paxos_paxid_pack(&py, pax.ibase);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  // End the sync.
  g_free(pax.sync);
  pax.sync = NULL;

  return 0;
}

int
acceptor_ack_truncate(struct paxos_header *hdr, msgpack_object *o)
{
  // Unpack the new ibase.
  paxos_paxid_unpack(&pax.ibase, o);

  // Do the truncate (< pax.ibase).
  ilist_truncate_prefix(&pax.ilist, pax.ibase);

  return 0;
}
