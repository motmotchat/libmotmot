/**
 * continuation.c - Utilities for Paxos continuations.
 */

#include <glib.h>

#include "paxos_state.h"
#include "containers/list_factory.h"
#include "types/continuation.h"

/**
 * Create a new continuation and add it to the list.
 */
struct paxos_continuation *
continuation_new(motmot_connect_continuation_t func, paxid_t paxid)
{
  struct paxos_continuation *k;

  k = g_malloc0(sizeof(*k));
  k->pk_cb.func = func;
  k->pk_cb.data = k;
  k->pk_session_id = pax->session_id;
  k->pk_paxid = paxid;

  LIST_INSERT_TAIL(&pax->clist, k, pk_le);

  return k;
}

void
continuation_destroy(struct paxos_continuation *k)
{
  g_free(k);
}

LIST_IMPLEMENT_DESTROY(continuation, pk_le, continuation_destroy);
