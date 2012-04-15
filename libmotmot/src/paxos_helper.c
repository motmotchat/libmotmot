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
//  Instance and instance list operations
//

/*
 * Free a instance fully.
 */
void
instance_free(struct paxos_instance *inst)
{
  g_free(inst->pi_val.pv_data);
  g_free(inst);
}

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
 * Add a decree.
 */
struct paxos_instance *
instance_add(struct instance_list *ilist, struct paxos_instance *inst)
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
