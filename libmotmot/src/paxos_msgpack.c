/**
 * paxos_msgpack.c - Msgpack helpers.
 */
#include "paxos.h"
#include "paxos_msgpack.h"

#include <glib.h>
#include <msgpack.h>

static void
msgpack_pack_paxid(msgpack_packer *pk, paxid_t paxid)
{
  msgpack_pack_uint32(pk, paxid);
}

void
paxos_payload_new(struct paxos_yak *py, size_t n)
{
  py->buf = msgpack_sbuffer_new();
  py->pk = msgpack_packer_new(py->buf, msgpack_sbuffer_write);

  msgpack_pack_array(py->pk, n);
}

void
paxos_payload_destroy(struct paxos_yak *py)
{
  msgpack_packer_free(py->pk);
  msgpack_sbuffer_free(py->buf);
}

char *
paxos_payload_data(struct paxos_yak *py)
{
  return py->buf->data;
}

size_t
paxos_payload_size(struct paxos_yak *py)
{
  return py->buf->size;
}

void
paxos_hdr_pack(struct paxos_yak *py, struct paxos_hdr *hdr)
{
  msgpack_pack_array(py->pk, 4);
  msgpack_pack_paxid(py->pk, hdr->ph_ballot.id);
  msgpack_pack_paxid(py->pk, hdr->ph_ballot.gen);
  msgpack_pack_int(py->pk, hdr->ph_opcode);
  msgpack_pack_paxid(py->pk, hdr->ph_inst);
}

struct paxos_hdr *
paxos_hdr_unpack(msgpack_object *o)
{
  struct paxos_hdr *hdr;
  struct msgpack_object *p;

  hdr = g_malloc(sizeof(struct paxos_hdr));
  if (hdr == NULL) {
    return NULL;
  }

  p = o->via.array.ptr;
  hdr->ph_ballot.id = p->via.u64;
  hdr->ph_ballot.gen = (++p)->via.u64;
  hdr->ph_opcode = (++p)->via.u64;
  hdr->ph_inst = (++p)->via.u64;

  return hdr;
}

void
paxos_val_pack(struct paxos_yak *py, struct paxos_val *val)
{
  msgpack_pack_array(py->pk, 4);
  msgpack_pack_int(py->pk, val->pv_dkind);
  msgpack_pack_paxid(py->pk, val->pv_paxid);
  msgpack_pack_raw(py->pk, val->pv_size);
  msgpack_pack_raw_body(py->pk, val->pv_data, val->pv_size);
}

struct paxos_val *
paxos_val_unpack(msgpack_object *o)
{
  struct paxos_val *val;
  struct msgpack_object *p;

  val = g_malloc(sizeof(struct paxos_val));
  if (val == NULL) {
    return NULL;
  }

  p = o->via.array.ptr;
  val->pv_dkind = p->via.u64;
  val->pv_paxid = (++p)->via.u64;
  val->pv_size = (++p)->via.raw.size;

  val->pv_data = g_malloc(p->via.raw.size);
  if (val->pv_data == NULL) {
    g_free(val);
    return NULL;
  }
  memcpy(val->pv_data, p->via.raw.ptr, p->via.raw.size);

  return val;
}
