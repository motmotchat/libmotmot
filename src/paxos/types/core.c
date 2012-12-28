/**
 * core.c - Utilities for Paxos core types.
 */

#include "paxos_state.h"
#include "types/core.h"
#include "util/paxos_msgpack.h"

///////////////////////////////////////////////////////////////////////////////
//
//  Paxos header utilities.
//

/**
 * Compare two ballot numbers.
 */
int
ballot_compare(ballot_t x, ballot_t y)
{
  return ppair_compare(x, y);
}

/**
 * Initialize a header based on the current session.
 */
void
header_init(struct paxos_header *hdr, paxop_t opcode, paxid_t inum)
{
  hdr->ph_session = *pax->session_id;
  hdr->ph_ballot.id = pax->ballot.id;
  hdr->ph_ballot.gen = pax->ballot.gen;
  hdr->ph_opcode = opcode;
  hdr->ph_inum = inum;
}

///////////////////////////////////////////////////////////////////////////////
//
//  Paxos value utilities.
//

int
reqid_compare(reqid_t x, reqid_t y)
{
  return ppair_compare(x, y);
}

///////////////////////////////////////////////////////////////////////////////
//
//  Msgpack utilities.
//

void
paxos_header_pack(struct paxos_yak *py, struct paxos_header *hdr)
{
  msgpack_pack_array(py->pk, 5);
  paxos_uuid_pack(py, &hdr->ph_session);
  msgpack_pack_paxid(py->pk, hdr->ph_ballot.id);
  msgpack_pack_paxid(py->pk, hdr->ph_ballot.gen);
  msgpack_pack_int(py->pk, hdr->ph_opcode);
  msgpack_pack_paxid(py->pk, hdr->ph_inum);
}

void
paxos_header_unpack(struct paxos_header *hdr, msgpack_object *o)
{
  msgpack_object *p;

  // Make sure the input is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 5);

  p = o->via.array.ptr;
  paxos_uuid_unpack(&hdr->ph_session, p++);
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  hdr->ph_ballot.id = (p++)->via.u64;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  hdr->ph_ballot.gen = (p++)->via.u64;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  hdr->ph_opcode = (p++)->via.u64;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  hdr->ph_inum = (p++)->via.u64;
}

void
paxos_value_pack(struct paxos_yak *py, struct paxos_value *val)
{
  msgpack_pack_array(py->pk, 4);
  msgpack_pack_int(py->pk, val->pv_dkind);
  msgpack_pack_paxid(py->pk, val->pv_reqid.id);
  msgpack_pack_paxid(py->pk, val->pv_reqid.gen);
  msgpack_pack_paxid(py->pk, val->pv_extra);
}

void
paxos_value_unpack(struct paxos_value *val, msgpack_object *o)
{
  msgpack_object *p;

  // Make sure the input is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 4);

  p = o->via.array.ptr;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  val->pv_dkind = (p++)->via.u64;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  val->pv_reqid.id = (p++)->via.u64;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  val->pv_reqid.gen = (p++)->via.u64;
  assert(p->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  val->pv_extra = (p++)->via.u64;
}
