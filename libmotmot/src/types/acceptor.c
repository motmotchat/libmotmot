/**
 * acceptor.c - Utilities for Paxos acceptors.
 */

#include <assert.h>
#include <glib.h>

#include "paxos_io.h"

#include "containers/list_factory.h"
#include "types/acceptor.h"

LIST_IMPLEMENT(acceptor, paxid_t, pa_le, pa_paxid, paxid_compare,
    acceptor_destroy, _FWD, _REV);

void
acceptor_destroy(struct paxos_acceptor *acc)
{
  if (acc != NULL) {
    paxos_peer_destroy(acc->pa_peer);
    g_free(acc->pa_desc);
  }
  g_free(acc);
}

///////////////////////////////////////////////////////////////////////////
//
//  Msgpack helpers.
//

void
paxos_acceptor_pack(struct paxos_yak *py, struct paxos_acceptor *acc)
{
  msgpack_pack_array(py->pk, 2);
  msgpack_pack_paxid(py->pk, acc->pa_paxid);
  msgpack_pack_raw(py->pk, acc->pa_size);
  msgpack_pack_raw_body(py->pk, acc->pa_desc, acc->pa_size);
}

void
paxos_acceptor_unpack(struct paxos_acceptor *acc, msgpack_object *o)
{
  msgpack_object *p;

  // Make sure the input is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 2);

  p = o->via.array.ptr;

  acc->pa_peer = NULL;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  acc->pa_paxid = (p++)->via.u64;
  assert(p->type == MSGPACK_OBJECT_RAW);
  acc->pa_size = p->via.raw.size;
  acc->pa_desc = g_memdup(p->via.raw.ptr, p->via.raw.size);
}
