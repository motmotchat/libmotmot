/**
 * paxos_learn.c - Commit and learn protocol for Paxos.
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
 * paxos_learn - Do something useful with the value of a commit.
 *
 * Note that we cannot free up the instance or any request associated with
 * it until a sync.
 */
int
paxos_learn(struct paxos_instance *inst)
{
  int was_proposer, deferred;
  struct paxos_request *req = NULL;
  struct paxos_acceptor *acc;

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
