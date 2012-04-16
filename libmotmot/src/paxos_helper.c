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
      break;
    }
  }

  // Insert into the list, sorted.
  LIST_INSERT_AFTER(ilist, it, inst, pi_le);
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
request_add(struct request_list *rlist, struct paxos_request *req)
{
  struct paxos_request *it;

  // We're probably ~appending.
  LIST_FOREACH_REV(it, rlist, pr_le) {
    if (req->pr_val.pv_srcid == it->pr_val.pv_srcid &&
        req->pr_val.pv_reqid == it->pr_val.pv_reqid) {
      return it;
    } else if (req->pr_val.pv_srcid >= it->pr_val.pv_srcid &&
               req->pr_val.pv_reqid > it->pr_val.pv_reqid) {
      break;
    }
  }

  // Insert into the list, sorted.
  LIST_INSERT_AFTER(rlist, it, req, pr_le);
  return req;
}


///////////////////////////////////////////////////////////////////////////
//
//  Acceptor list operations
//

/*
 * Broadcast a message to all acceptors.
 */
int
paxos_broadcast(char *buf, size_t size)
{
  struct paxos_acceptor *acc;
  GError *gerr;
  GIOStatus status;
  unsigned long len;

  LIST_FOREACH(acc, &(pax.alist), pa_le) {
    if (acc->pa_chan == NULL) {
      continue;
    }

    gerr = NULL;
    status = g_io_channel_write_chars(acc->pa_chan, buf, size, &len, &gerr);
    if (status == G_IO_STATUS_ERROR) {
      // TODO: error handling
      dprintf(2, "proposer_prepare: Could not write prepare to socket.\n");
    }

    gerr = NULL;
    status = g_io_channel_flush(acc->pa_chan, &gerr);
    if (status == G_IO_STATUS_ERROR) {
      // TODO: error handling
      dprintf(2, "proposer_prepare: Could not flush prepare on socket.\n");
    }
  }

  return 0;
}
