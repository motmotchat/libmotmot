/**
 * session_local.c - Utilities for Paxos acceptors, instances, and requests
 * along with factorized container implementations.
 */

#include <glib.h>

#include "paxos_io.h"

#include "containers/list_factory.h"
#include "types/session_local.h"

/**
 * Reset the metadata fields of a Paxos instance.
 */
void
instance_init_metadata(struct paxos_instance *inst)
{
  inst->pi_committed = false;
  inst->pi_cached = false;
  inst->pi_learned = false;
  inst->pi_votes = 1;
  inst->pi_rejects = 0;
}

///////////////////////////////////////////////////////////////////////////
//
//  Container manufacturing.
//

LIST_IMPLEMENT(acceptor, paxid_t, pa_le, pa_paxid, paxid_compare,
    acceptor_destroy, _FWD, _REV);
LIST_IMPLEMENT(instance, paxid_t, pi_le, pi_hdr.ph_inum, paxid_compare,
    instance_destroy, _REV, _REV);
LIST_IMPLEMENT(request, reqid_t, pr_le, pr_val.pv_reqid, reqid_compare,
    request_destroy, _FWD, _REV);

///////////////////////////////////////////////////////////////////////////
//
//  Destructor routines.
//

void
acceptor_destroy(struct paxos_acceptor *acc)
{
  if (acc != NULL) {
    paxos_peer_destroy(acc->pa_peer);
    g_free(acc->pa_desc);
  }
  g_free(acc);
}

void
instance_destroy(struct paxos_instance *inst)
{
  g_free(inst);
}

void
request_destroy(struct paxos_request *req)
{
  if (req != NULL) {
    g_free(req->pr_data);
  }
  g_free(req);
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

void
paxos_instance_pack(struct paxos_yak *py, struct paxos_instance *inst)
{
  msgpack_pack_array(py->pk, 3);
  paxos_header_pack(py, &inst->pi_hdr);
  inst->pi_committed ? msgpack_pack_true(py->pk) : msgpack_pack_false(py->pk);
  paxos_value_pack(py, &inst->pi_val);
}

void
paxos_instance_unpack(struct paxos_instance *inst, msgpack_object *o)
{
  msgpack_object *p;

  // Make sure the input is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 3);

  p = o->via.array.ptr;

  // Unpack the header, committed flag, and value.
  paxos_header_unpack(&inst->pi_hdr, p++);
  assert(p->type == MSGPACK_OBJECT_BOOLEAN);
  inst->pi_committed = (p++)->via.boolean;
  paxos_value_unpack(&inst->pi_val, p++);

  // Set everything else to 0.
  inst->pi_cached = false;
  inst->pi_learned = false;
  inst->pi_votes = 0;
  inst->pi_rejects = 0;
}

void
paxos_request_pack(struct paxos_yak *py, struct paxos_request *req)
{
  msgpack_pack_array(py->pk, 2);
  paxos_value_pack(py, &req->pr_val);
  msgpack_pack_raw(py->pk, req->pr_size);
  msgpack_pack_raw_body(py->pk, req->pr_data, req->pr_size);
}

void
paxos_request_unpack(struct paxos_request *req, msgpack_object *o)
{
  msgpack_object *p;

  // Make sure the input is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 2);

  // Unpack the value.
  p = o->via.array.ptr;
  paxos_value_unpack(&req->pr_val, p++);

  // Unpack the raw data.
  assert(p->type == MSGPACK_OBJECT_RAW);
  req->pr_size = p->via.raw.size;
  req->pr_data = g_memdup(p->via.raw.ptr, p->via.raw.size);
}
