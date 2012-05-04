/**
 * paxos_learn.c - Commit and learn protocol for Paxos.
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
  int r;
  struct paxos_request *req = NULL;
  struct paxos_instance *it;

  // Mark the commit.
  inst->pi_committed = true;

  // Pull the request from the request cache if applicable.
  if (request_needs_cached(inst->pi_val.pv_dkind)) {
    req = request_find(&pax->rcache, inst->pi_val.pv_reqid);

    // If we can't find a request and need one, send out a retrieve to the
    // request originator and defer the commit.
    if (req == NULL) {
      return paxos_retrieve(inst);
    }
  }

  // Mark the cache.
  inst->pi_cached = true;

  // We should already have committed and learned everything before the hole.
  assert(inst->pi_hdr.ph_inum >= pax->ihole);

  // Since we want our learns to be totally ordered, if we didn't just fill
  // the hole, we cannot learn.
  if (inst->pi_hdr.ph_inum != pax->ihole) {
    // If we're the proposer, we have to just wait it out.
    if (is_proposer()) {
      return 0;
    }

    // If the hole has committed but is just waiting on a retrieve, we'll learn
    // when we receive the resend.
    if (pax->istart->pi_hdr.ph_inum == pax->ihole && pax->istart->pi_committed) {
      assert(!pax->istart->pi_cached);
      return 0;
    }

    // The hole is either missing or uncommitted and we are not the proposer,
    // so issue a retry.
    return acceptor_retry(pax->ihole);
  }

  // Set pax->istart to point to the instance numbered pax->ihole.
  if (pax->istart->pi_hdr.ph_inum != pax->ihole) {
    pax->istart = LIST_NEXT(pax->istart, pi_le);
  }
  assert(pax->istart->pi_hdr.ph_inum == pax->ihole);

  // Now learn as many contiguous commits as we can.  This function is the
  // only path by which we learn commits, and we always learn in contiguous
  // blocks.  Therefore, it is an invariant of our system that all the
  // instances numbered lower than pax->ihole are learned and committed, and
  // none of the instances geq to pax->ihole are learned (although some may
  // be committed).
  //
  // We iterate over the instance list, detecting and breaking if we find a
  // hole and learning whenever we don't.
  for (it = pax->istart; ; it = LIST_NEXT(it, pi_le), ++pax->ihole) {
    // If we reached the end of the list, set pax->istart to the last existing
    // instance.
    if (it == (void *)&pax->ilist) {
      pax->istart = LIST_LAST(&pax->ilist);
      break;
    }

    // If we skipped over an instance number because we were missing an
    // instance, set pax->istart to the last instance before the hole.
    if (it->pi_hdr.ph_inum != pax->ihole) {
      pax->istart = LIST_PREV(it, pi_le);
      break;
    }

    // If we found an uncommitted or uncached instance, set pax->istart to it.
    if (!it->pi_committed || !it->pi_cached) {
      pax->istart = it;
      break;
    }

    // By our invariant, since we are past our original hole, no instance
    // should be learned.
    assert(!it->pi_learned);

    // Grab its associated request.  This is guaranteed to exist because we
    // have checked that pi_cached holds.
    req = NULL;
    if (request_needs_cached(it->pi_val.pv_dkind)) {
      req = request_find(&pax->rcache, it->pi_val.pv_reqid);
      assert(req != NULL);
    }

    // Learn the value.
    ERR_RET(r, paxos_learn(it, req));
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
  int was_proposer;
  struct paxos_acceptor *acc;

  // Mark the learn.
  inst->pi_learned = true;

  // Act on the decree (e.g., display chat, record acceptor list changes).
  switch (inst->pi_val.pv_dkind) {
    case DEC_NULL:
      break;

    case DEC_CHAT:
      // Invoke client learning callback.
      state.learn.chat(req->pr_data, req->pr_size, pax->client_data);
      break;

    case DEC_JOIN:
      // Check the adefer list to see if we received a hello already for the
      // newly joined acceptor.
      acc = acceptor_find(&pax->adefer, inst->pi_hdr.ph_inum);

      if (acc != NULL) {
        // We found a deferred hello.  To complete the hello, just move our
        // acceptor over to the alist and increment the live count.
        LIST_REMOVE(&pax->adefer, acc, pa_le);
        pax->live_count++;
      } else {
        // We have not yet gotten the hello, so create a new acceptor.
        acc = g_malloc0(sizeof(*acc));
        acc->pa_paxid = inst->pi_hdr.ph_inum;
      }
      acceptor_insert(&pax->alist, acc);

      // Copy over the identity information.
      acc->pa_size = req->pr_size;
      acc->pa_desc = g_memdup(req->pr_data, req->pr_size);

      // If we are the proposer, we are responsible for connecting to the new
      // acceptor, as well as for sending the new acceptor its paxid and other
      // initial data.
      if (is_proposer()) {
        proposer_welcome(acc);
      }

      // Invoke client learning callback.
      state.learn.join(req->pr_data, req->pr_size, pax->client_data);
      break;

    case DEC_PART:
      // Are we the proposer right now?
      was_proposer = is_proposer();

      // Pull the acceptor from the alist.
      acc = acceptor_find(&pax->alist, inst->pi_val.pv_extra);
      if (acc == NULL) {
        // It is possible that we may part twice; for instance, if a proposer
        // issues a part for itself but its departure from the system is
        // detected by acceptors before the part commit is received.  In this
        // case, just do nothing.
        break;
      }

      // Invoke client learning callback.
      state.learn.part(acc->pa_desc, acc->pa_size, pax->client_data);

      if (acc->pa_paxid != pax->self_id) {
        // Just clean up the acceptor.
        LIST_REMOVE(&pax->alist, acc, pa_le);
        acceptor_destroy(acc);
      } else {
        // We are leaving the protocol, so wipe all our state clean.
        paxos_end(&pax);
        return 1;
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
