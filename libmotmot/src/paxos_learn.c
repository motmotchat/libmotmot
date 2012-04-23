/**
 * paxos_learn.c - Commit and learn protocol for Paxos.
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
 * paxos_commit - Commit a value for an instance of the Paxos protocol.
 *
 * We totally order calls to paxos_learn by instance number in order to make
 * the join and greet protocols behave properly.  This also gives our chat
 * clients an easy mechanism for totally ordering their logs without extra
 * work on their part.
 *
 * It is possible that failed DEC_PART decrees (i.e., decrees in which the
 * proposer attempts to disconnect an acceptor who a majority of acceptors
 * believe is still alive) could delay the learning of committed chat
 * messages.  To avoid this, once a proposer receives enough rejections
 * of the decree, the part decree is replaced with a null decree.  The
 * proposer can then issue the part again with a higher instance number
 * if desired.
 */
int
paxos_commit(struct paxos_instance *inst)
{
  struct paxos_request *req = NULL;
  struct paxos_instance *it;

  // Pull the request from the request cache if applicable.
  if (request_needs_cached(inst->pi_val.pv_dkind)) {
    req = request_find(&pax.rcache, inst->pi_val.pv_reqid);

    // If we can't find a request and need one, send out a retrieve to the
    // request originator and defer the commit.
    if (req == NULL) {
      return paxos_retrieve(inst);
    }
  }

  // Mark the commit.
  inst->pi_votes = 0;

  // We should already have committed and learned everything before the hole.
  assert(inst->pi_hdr.ph_inum >= pax.ihole);

  // Check if we just committed the hole, returning if we didn't.
  if (inst->pi_hdr.ph_inum != pax.ihole) {
    return 0;
  }

  // If we did just fill in the hole, learn it.
  paxos_learn(inst, req);

  // Set pax.istart to point to the instance numbered pax.ihole.
  while (pax.istart->pi_hdr.ph_inum != pax.ihole) {
    pax.istart = LIST_NEXT(pax.istart, pi_le);
  }

  // Now learn as many contiguous commits as we can.  This function is the
  // only path by which we learn commits, and we always learn in contiguous
  // blocks.  Therefore, it is an invariant of our system that all the
  // instances numbered lower than pax.ihole are learned and committed, and
  // none of the instances geq to pax.ihole are learned (although some may
  // be committed).

  // Our first comparison will be between the next instance number after
  // pax.ihole and the next instance after pax.istart.
  it = LIST_NEXT(pax.istart, pi_le);
  ++pax.ihole;

  // Iterate over the instance list, detecting and breaking if we find a hole
  // and learning whenever we don't.
  for (; ; it = LIST_NEXT(it, pi_le), ++pax.ihole) {
    // If we reached the end of the list, set pax.istart to the last existing
    // instance.
    if (it == (void *)&pax.ilist) {
      pax.istart = LIST_LAST(&pax.ilist);
      break;
    }

    // If we skipped over an instance number because we were missing an
    // instance, set pax.istart to the last instance before the hole.
    if (it->pi_hdr.ph_inum != pax.ihole) {
      pax.istart = LIST_PREV(it, pi_le);
      break;
    }

    // If we found an uncommitted instance, set pax.istart to it.
    if (it->pi_votes != 0) {
      pax.istart = it;
      break;
    }

    // Otherwise, we've found a previously unlearned but committed instance,
    // so learn it.  Note that an instance only commits if its associated
    // request object was cached.
    req = NULL;
    if (request_needs_cached(it->pi_val.pv_dkind)) {
      req = request_find(&pax.rcache, it->pi_val.pv_reqid);
      assert(req != NULL);
    }
    paxos_learn(it, req);
  }

  return 0;
}

/**
 * paxos_learn - Do something useful with the value of a commit.
 *
 * Note that we cannot free up the instance or any request associated with
 * it until a sync.
 */
int
paxos_learn(struct paxos_instance *inst, struct paxos_request *req)
{
  int was_proposer, deferred;
  struct paxos_acceptor *acc;

  // Act on the decree (e.g., display chat, record acceptor list changes).
  switch (inst->pi_val.pv_dkind) {
    case DEC_NULL:
      break;

    case DEC_CHAT:
      // Invoke client learning callback.
      pax.learn.chat(req->pr_data, req->pr_size);
      break;

    case DEC_JOIN:
      // Finding an existing acceptor object with the new acceptor's ID means
      // that we received a greet order from the proposer for this join before
      // we actually committed it.  In this case, we do not reinitialize and
      // send out the deferred hello.
      acc = acceptor_find(&pax.alist, inst->pi_hdr.ph_inum);
      deferred = (acc != NULL);

      // If we don't find anything, initialize a new acceptor struct.  Its
      // paxid is the instance number of the join.
      if (!deferred) {
        acc = g_malloc0(sizeof(*acc));
        acc->pa_paxid = inst->pi_hdr.ph_inum;
        acceptor_insert(&pax.alist, acc);
      }

      // Initialize a paxos_peer via a callback.
      acc->pa_peer = paxos_peer_init(pax.connect(req->pr_data, req->pr_size));

      // Copy over the identity information.
      acc->pa_size = req->pr_size;
      acc->pa_desc = g_memdup(req->pr_data, req->pr_size);

      // If we are the proposer, send the new acceptor its paxid.
      if (is_proposer()) {
        proposer_welcome(acc);
      }

      // Send the deferred hello if necessary.
      if (deferred) {
        acceptor_hello(acc);
      }

      // Invoke client learning callback.
      pax.learn.join(req->pr_data, req->pr_size);
      break;

    case DEC_PART:
      // The pv_extra field tells us who is parting (possibly a forced part).
      // If it is 0, it is a user request to self-part.
      if (inst->pi_val.pv_extra == 0) {
        inst->pi_val.pv_extra = inst->pi_val.pv_reqid.id;
      }

      // Are we the proposer right now?
      was_proposer = is_proposer();

      // Pull the acceptor from the alist.
      acc = acceptor_find(&pax.alist, inst->pi_val.pv_extra);
      if (acc == NULL) {
        // It is possible that we may part twice; for instance, if a proposer
        // issues a part for itself but its departure from the system is
        // detected by acceptors before the part commit is received.  In this
        // case, just do nothing.
        break;
      }

      // Invoke client learning callback.
      pax.learn.part(acc->pa_desc, acc->pa_size);

      if (acc->pa_paxid != pax.self_id) {
        // Just clean up the acceptor.
        LIST_REMOVE(&pax.alist, acc, pa_le);
        acceptor_destroy(acc);
      } else {
        // We are leaving the protocol, so wipe all our state clean.
        paxos_end();
      }

      // Prepare if we became the proposer as a result of the part.
      reset_proposer();
      if (!was_proposer && is_proposer()) {
        proposer_prepare();
      }

      break;
  }

  return 0;
}
