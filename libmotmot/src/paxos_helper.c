/**
 * paxos_helper.c - Helper functions for Paxos.
 */
#include "paxos.h"
#include "list.h"

#include <stdio.h>
#include <glib.h>

///////////////////////////////////////////////////////////////////////////
//
//  Paxos state helpers
//

/*
 * Check if we think we are the proposer.
 */
int
is_proposer()
{
  return pax.self_id == pax.proposer->pa_paxid;
}

/*
 * Gets the next free instance number.
 */
paxid_t
next_instance()
{
  return LIST_EMPTY(&pax.ilist) ? 1 : LIST_LAST(&pax.ilist)->pi_hdr.ph_inum + 1;
}

/*
 * Compare two ballot IDs.
 */
int
ballot_compare(ballot_t x, ballot_t y)
{
  // Compare generation first.
  if (x.gen < y.gen) {
    return -1;
  } else if (x.gen > y.gen) {
    return 1;
  }

  /* x.gen == y.gen */
  if (x.id < y.id) {
    return -1;
  } else if (x.id > y.id) {
    return 1;
  } else /* x.id == y.id */  {
    return 0;
  }
}


///////////////////////////////////////////////////////////////////////////
//
//  Instance and request list operations
//

/*
 * Find an instance by its instance number.
 */
struct paxos_instance *
instance_find(struct instance_list *ilist, paxid_t inum)
{
  struct paxos_instance *it;

  // We assume we want a more recent instance, so we search in reverse.
  LIST_FOREACH_REV(it, ilist, pi_le) {
    if (inum == it->pi_hdr.ph_inum) {
      return it;
    } else if (inum > it->pi_hdr.ph_inum) {
      break;
    }
  }

  return NULL;
}

/*
 * Add an instance.  If an instance with the same instance number already
 * exists, the list is not modified and the existing instance is returned.
 */
struct paxos_instance *
instance_insert(struct instance_list *ilist, struct paxos_instance *inst)
{
  struct paxos_instance *it;

  // We're probably ~appending.
  LIST_FOREACH_REV(it, ilist, pi_le) {
    if (inst->pi_hdr.ph_inum == it->pi_hdr.ph_inum) {
      return it;
    } else if (inst->pi_hdr.ph_inum > it->pi_hdr.ph_inum) {
      // Insert into the list, sorted.
      LIST_INSERT_AFTER(ilist, it, inst, pi_le);
      return inst;
    }
  }

  // We didn't find anything with a lower instance number, so just insert
  // at head.
  LIST_INSERT_HEAD(ilist, inst, pi_le);
  return inst;
}

/*
 * Find a request by its full request ID.
 */
struct paxos_request *
request_find(struct request_list *rlist, paxid_t srcid, paxid_t reqid)
{
  struct paxos_request *it;

  // We assume we want an earlier request, so we search forward.
  LIST_FOREACH(it, rlist, pr_le) {
    if (srcid == it->pr_val.pv_srcid && reqid == it->pr_val.pv_reqid) {
      return it;
    } else if (srcid <= it->pr_val.pv_srcid && reqid < it->pr_val.pv_reqid) {
      break;
    }
  }

  return NULL;
}

/*
 * Add a request.  If a request with the same request ID already exists, the
 * list is not modified and the existing request is returned.
 */
struct paxos_request *
request_insert(struct request_list *rlist, struct paxos_request *req)
{
  struct paxos_request *it;

  // We're probably ~appending.
  LIST_FOREACH_REV(it, rlist, pr_le) {
    if (req->pr_val.pv_srcid == it->pr_val.pv_srcid &&
        req->pr_val.pv_reqid == it->pr_val.pv_reqid) {
      return it;
    } else if (req->pr_val.pv_srcid >= it->pr_val.pv_srcid &&
               req->pr_val.pv_reqid > it->pr_val.pv_reqid) {
      LIST_INSERT_AFTER(rlist, it, req, pr_le);
      return req;
    }
  }

  // We didn't find anything with a lower request ID, so just insert at head.
  LIST_INSERT_HEAD(rlist, req, pr_le);
  return req;
}

/*
 * Convenience function for denoting which dkinds are requests.
 */
int
is_request(dkind_t dkind)
{
  return dkind == DEC_CHAT;
}


///////////////////////////////////////////////////////////////////////////
//
//  Acceptor list operations
//

/**
 * Broadcast a message to all acceptors.
 */
int
paxos_broadcast(const char *buffer, size_t length)
{
  struct paxos_acceptor *acc;
  int retval;

  LIST_FOREACH(acc, &(pax.alist), pa_le) {
    if (acc->pa_peer == NULL) {
      continue;
    }

    retval = paxos_peer_send(acc->pa_peer, buffer, length);
    if (retval) {
      return retval;
    }
  }

  return 0;
}

/**
 * Send a message to the proposer
 */
int
paxos_send_to_proposer(const char *buffer, size_t length)
{
  if (pax.proposer == NULL) {
    return 1;
  }

  return paxos_peer_send(pax.proposer->pa_peer, buffer, length);
}
