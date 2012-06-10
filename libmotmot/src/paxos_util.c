/**
 * paxos_util.c - Utility functions for Paxos which read and write to session
 * state.  Closer to the protocol than paxos_helper.c.
 */

#include <assert.h>
#include <glib.h>

#include "paxos.h"
#include "paxos_continue.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "paxos_util.h"
#include "list.h"

///////////////////////////////////////////////////////////////////////////
//
//  Primitives.
//

/**
 * is_proposer - Check if we think we are the proposer.
 */
int
is_proposer()
{
  return pax->proposer != NULL && pax->self_id == pax->proposer->pa_paxid;
}

/**
 * reset_proposer - Realias the proposer after an update to the acceptor list.
 */
void
reset_proposer()
{
  struct paxos_acceptor *it;

  LIST_FOREACH(it, &pax->alist, pa_le) {
    if (it->pa_paxid == pax->self_id || it->pa_peer != NULL) {
      pax->proposer = it;
      break;
    }
  }
}

/**
 * next_instance - Gets the next free instance number.
 */
paxid_t
next_instance()
{
  return LIST_EMPTY(&pax->ilist) ? 1 : LIST_LAST(&pax->ilist)->pi_hdr.ph_inum + 1;
}

/**
 * majority - Get the minimum number of acceptors needed in a simple majority.
 */
unsigned
majority()
{
  return (LIST_COUNT(&pax->alist) / 2) + 1;
}

/**
 * request_needs_cached - Convenience function for denoting which dkinds are
 * requests.
 */
int
request_needs_cached(dkind_t dkind)
{
  return (dkind == DEC_CHAT || dkind == DEC_JOIN);
}


///////////////////////////////////////////////////////////////////////////
//
//  Protocol utilities.
//

/**
 * instance_insert_and_upstart - Insert a newly allocated instance into the
 * ilist and update istart.
 */
void
instance_insert_and_upstart(struct paxos_instance *inst)
{
  // Insert into the ilist.
  instance_insert(&pax->ilist, inst);

  // Update istart if we just instantiated the hole.
  if (inst->pi_hdr.ph_inum == pax->ihole) {
    pax->istart = inst;
  }
}

/**
 * paxos_broadcast_instance - Pack the header and value of an instance and
 * broadcast.
 */
int
paxos_broadcast_instance(struct paxos_instance *inst)
{
  int r;
  struct paxos_yak py;

  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_value_pack(&py, &(inst->pi_val));
  r = paxos_broadcast(&py);
  paxos_payload_destroy(&py);

  return r;
}

/**
 * proposer_decree_part - Decree a part, deferring as necessary.
 */
int
proposer_decree_part(struct paxos_acceptor *acc, int force)
{
  struct paxos_instance *inst;

  inst = g_malloc0(sizeof(*inst));

  if (force) {
    inst->pi_val.pv_dkind = DEC_KILL;
  } else {
    inst->pi_val.pv_dkind = DEC_PART;
  }
  inst->pi_val.pv_reqid.id = pax->self_id;
  inst->pi_val.pv_reqid.gen = (++pax->req_id);
  inst->pi_val.pv_extra = acc->pa_paxid;

  if (pax->prep != NULL) {
    LIST_INSERT_TAIL(&pax->idefer, inst, pi_le);
    return 0;
  } else {
    return proposer_decree(inst);
  }
}
