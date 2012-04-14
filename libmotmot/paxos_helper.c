/**
 * paxos_helper.c - Helper functions for Paxos.
 */
#include "paxos.h"
#include "list.h"

#include <glib.h>
#include <msgpack.h>

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
  return pax.self_id == pax.proposer.id;
}

/*
 * Compare two proposer IDs.
 *
 * XXX: Should this use the presidency order?
 */
int
proposer_compare(proposer_t x, proposer_t y)
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
//  Decree list operations
//

/*
 * Find a decree by its instance number.
 */
struct paxos_decree *
decree_find(struct decree_list *dlist, paxid_t inst)
{
  struct paxos_decree *it;

  // We assume we're looking for more recent decrees, so we search in reverse.
  LIST_FOREACH_REV(it, &(pax.dlist), pd_list) {
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
  dec->pd_votes = 1;
  dec->pd_val.pv_dkind = val->pv_dkind;
  dec->pd_val.pv_paxid = val->pv_paxid;

  dec->pd_val.pv_data = g_malloc(val->pv_size);
  memcpy(dec->pd_val.pv_data, val->pv_data, val->pv_size);

  // We're probably ~appending.
  LIST_FOREACH_REV(it, &(pax.dlist), pd_list) {
    if (dec->pd_hdr.ph_inst == it->pd_hdr.ph_inst) {
      g_free(dec->pd_val.pv_data);
      g_free(dec);
      return -1;
    } else if (dec->pd_hdr.ph_inst > it->pd_hdr.ph_inst) {
      break;
    }
  }

  // Insert into the list, sorted.
  LIST_INSERT_AFTER(&(pax.dlist), it, dec, pd_list);
  return 0;
}


///////////////////////////////////////////////////////////////////////////
//
//  Msgpack helpers routines
//

/*
 * Pack a paxid_t.
 */
void
msgpack_pack_paxid(msgpack_packer *pk, paxid_t paxid)
{
  msgpack_pack_uint32(pk, paxid);
}

/*
 * Pack a Paxos header into a msgpack buffer.
 */
void
paxos_hdr_pack(msgpack_packer *pk, struct paxos_hdr *hdr)
{
  msgpack_pack_array(pk, 4);
  msgpack_pack_paxid(pk, hdr->ph_prop.id);
  msgpack_pack_paxid(pk, hdr->ph_prop.gen);
  msgpack_pack_int(pk, hdr->ph_opcode);
  msgpack_pack_paxid(pk, hdr->ph_inst);
}

/*
 * Unpack a Paxos header from a msgpack object.
 */
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
  hdr->ph_prop.id = p->via.u64;
  hdr->ph_prop.gen = (++p)->via.u64;
  hdr->ph_opcode = (++p)->via.u64;
  hdr->ph_inst = (++p)->via.u64;

  return hdr;
}

/*
 * Pack a Paxos value into a msgpack buffer.
 */
void
paxos_val_pack(msgpack_packer *pk, struct paxos_val *val)
{
  msgpack_pack_array(pk, 4);
  msgpack_pack_int(pk, val->pv_dkind);
  msgpack_pack_paxid(pk, val->pv_paxid);
  msgpack_pack_raw(pk, val->pv_size);
  msgpack_pack_raw_body(pk, val->pv_data, val->pv_size);
}

/*
 * Unpack a Paxos value from a msgpack object.
 */
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
