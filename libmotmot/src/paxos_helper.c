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
//  Decree and decree list operations
//

/*
 * Free a decree fully.
 */
void
decree_free(struct paxos_decree *dec)
{
  g_free(dec->pd_val.pv_data);
  g_free(dec);
}

/*
 * Find a decree by its instance number.
 */
struct paxos_decree *
decree_find(struct decree_list *dlist, paxid_t inst)
{
  struct paxos_decree *it;

  // We assume we're looking for more recent decrees, so we search in reverse.
  LIST_FOREACH_REV(it, dlist, pd_le) {
    if (inst == it->pd_hdr.ph_inst) {
      return it;
    } else if (inst > it->pd_hdr.ph_inst) {
      break;
    }
  }

  return NULL;
}

/*
 * Add a decree.
 */
int
decree_add(struct decree_list *dlist, struct paxos_hdr *hdr,
    struct paxos_val *val)
{
  struct paxos_decree *dec, *it;

  // Allocate and initialize a new decree.
  dec = g_malloc0(sizeof(struct paxos_decree));
  if (dec == NULL) {
    return -1;
  }

  dec->pd_hdr = *hdr;
  dec->pd_votes = 1;  // All decrees start out with 1 vote -OR- uncommited.
  dec->pd_val.pv_dkind = val->pv_dkind;
  dec->pd_val.pv_paxid = val->pv_paxid;

  dec->pd_val.pv_data = g_malloc(val->pv_size);
  memcpy(dec->pd_val.pv_data, val->pv_data, val->pv_size);

  // We're probably ~appending.
  LIST_FOREACH_REV(it, dlist, pd_le) {
    if (dec->pd_hdr.ph_inst == it->pd_hdr.ph_inst) {
      g_free(dec->pd_val.pv_data);
      g_free(dec);
      return -1;
    } else if (dec->pd_hdr.ph_inst > it->pd_hdr.ph_inst) {
      break;
    }
  }

  // Insert into the list, sorted.
  LIST_INSERT_AFTER(dlist, it, dec, pd_le);
  return 0;
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
