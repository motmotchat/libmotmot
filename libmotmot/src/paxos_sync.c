/**
 * paxos_sync.c - Log synchronization for Paxos.
 */

#include <assert.h>
#include <glib.h>

#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "paxos_util.h"
#include "list.h"

#define SYNC_SKIP_THRESH  30

/**
 * paxos_sync - GEvent-friendly wrapper around proposer_sync.
 */
int paxos_sync(void *data)
{
  pax_uuid_t *uuid;

  // Set the session.  We parametrize paxos_sync with a pointer to a session
  // ID when we add it to the main event loop.
  uuid = (pax_uuid_t *)data;
  pax = session_find(&state.sessions, *uuid);

  if (is_proposer()) {
    proposer_sync();
  }

  return TRUE;
}

/**
 * proposer_sync - Send a sync command to all acceptors.
 *
 * For a sync to succeed, all acceptors need to tell us the instance number
 * of their last contiguous learn.  We take the minimum of these values
 * and then command everyone to truncate everything before this minimum.
 */
int
proposer_sync()
{
  int r;
  struct paxos_header hdr;
  struct paxos_yak py;

  // If we haven't finished preparing as the proposer, don't sync.
  if (pax->prep != NULL) {
    return 1;
  }

  // If not everyone is live, we should delay syncing.
  if (pax->live_count != LIST_COUNT(&pax->alist)) {
    return 1;
  }

  // If our local last contiguous learn is the same as the previous sync
  // point, we don't need to sync.
  if (pax->ihole - 1 == pax->sync_prev) {
    return 0;
  }

  // If we're already syncing, increment the skip counter.
  if (pax->sync != NULL) {
    // Resync if we've waited too long for the sync to finish.
    if (++pax->sync->ps_skips < SYNC_SKIP_THRESH) {
      return 1;
    } else {
      g_free(pax->sync);
    }
  }

  // Create a new sync.
  pax->sync = g_malloc0(sizeof(*(pax->sync)));
  pax->sync->ps_total = LIST_COUNT(&pax->alist);
  pax->sync->ps_acks = 1;  // Including ourselves.
  pax->sync->ps_skips = 0;
  pax->sync->ps_last = 0;

  // Initialize a header.
  header_init(&hdr, OP_SYNC, ++pax->sync_id);

  // Pack and broadcast the sync.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  r = paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * acceptor_ack_sync - Respond to the sync request of a proposer.
 *
 * We treat the sync like a decree and respond only if it has the appropriate
 * ballot number.
 */
int
acceptor_ack_sync(struct paxos_header *hdr)
{
  // Respond only if the ballot number matches ours.
  if (ballot_compare(hdr->ph_ballot, pax->ballot) == 0) {
    return acceptor_last(hdr);
  } else {
    return 0;
  }
}

/**
 * acceptor_last - Send the instance number of our last contiguous learn to
 * the proposer.
 */
int acceptor_last(struct paxos_header *hdr)
{
  int r;
  struct paxos_yak py;

  // Modify the header opcode.
  hdr->ph_opcode = OP_LAST;

  // Pack and send the response.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);
  paxos_paxid_pack(&py, pax->ihole - 1);
  r = paxos_send_to_proposer(UNYAK(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * proposer_ack_last - Update sync state based on acceptor's reply.
 */
int
proposer_ack_last(struct paxos_header *hdr, msgpack_object *o)
{
  paxid_t last;

  // Ignore replies to older sync commands.
  if (hdr->ph_inum != pax->sync_id) {
    return 0;
  }

  // Update our knowledge of the system's last contiguous learn.
  paxos_paxid_unpack(&last, o);
  if (last < pax->sync->ps_last || pax->sync->ps_last == 0) {
    pax->sync->ps_last = last;
  }

  // Increment acks and command a truncate if the sync is over.
  pax->sync->ps_acks++;
  if (pax->sync->ps_acks == pax->sync->ps_total) {
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
  struct paxos_instance *it;
  struct paxos_request *req;

  for (it = LIST_FIRST(ilist); it != (void *)ilist; it = LIST_FIRST(ilist)) {
    // Break if we've hit the desired stopping point.
    if (it->pi_hdr.ph_inum >= inum) {
      break;
    }

    // Free the instance and its associated request.
    req = request_find(&pax->rcache, it->pi_val.pv_reqid);
    if (req != NULL) {
      LIST_REMOVE(&pax->rcache, req, pr_le);
      request_destroy(req);
    }
    LIST_REMOVE(ilist, it, pi_le);
    instance_destroy(it);
  }
}

/**
 * proposer_truncate - Command all acceptors to drop the contiguous prefix
 * of Paxos instances for which every participant has learned.
 */
int
proposer_truncate(struct paxos_header *hdr)
{
  int r;
  struct paxos_yak py;

  // Obtain our own last contiguous learn.
  if (pax->ihole - 1 < pax->sync->ps_last) {
    pax->sync->ps_last = pax->ihole - 1;
  }

  // Record the sync point.
  pax->sync_prev = pax->sync->ps_last;

  // Make this instance our new ibase; this ensures that our list always has
  // at least one committed instance.
  assert(pax->sync->ps_last >= pax->ibase);
  pax->ibase = pax->sync->ps_last;

  // Modify the header.
  hdr->ph_opcode = OP_TRUNCATE;

  // Pack a truncate.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);
  paxos_paxid_pack(&py, pax->ibase);

  // Broadcast it.
  r = paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);
  if (r) {
    return r;
  }

  // Do the truncate (< pax->ibase).
  ilist_truncate_prefix(&pax->ilist, pax->ibase);

  // End the sync.
  g_free(pax->sync);
  pax->sync = NULL;

  return 0;
}

int
acceptor_ack_truncate(struct paxos_header *hdr, msgpack_object *o)
{
  // Unpack the new ibase.
  paxos_paxid_unpack(&pax->ibase, o);

  // Do the truncate (< pax->ibase).
  ilist_truncate_prefix(&pax->ilist, pax->ibase);

  return 0;
}
