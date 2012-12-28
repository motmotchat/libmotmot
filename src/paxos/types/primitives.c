/**
 * primitives.c - Utilities for Paxos primitive types.
 */

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "types/primitives.h"
#include "util/paxos_msgpack.h"

///////////////////////////////////////////////////////////////////////////
//
//  Comparison functions.
//

/**
 * Compare two paxid's.
 */
int
paxid_compare(paxid_t x, paxid_t y)
{
  if (x < y) {
    return -1;
  } else if (x > y) {
    return 1;
  } else {
    return 0;
  }
}

/**
 * Compare two paxid pairs.
 *
 * The first coordinate is subordinate to the second.  The second compares
 * normally (x.gen < y.gen ==> -1) but the first compares inversely (x.id
 * < y.id ==> 1)
 */
int
ppair_compare(ppair_t x, ppair_t y)
{
  int r = paxid_compare(x.gen, y.gen);
  if (r) { return r; }
  return paxid_compare(y.id, x.id);
}

/**
 * Compare two strings.  Uh, I mean _Paxos_ strings.
 */
int
pax_str_compare(pax_str_t *x, pax_str_t *y) {
  if (x->size < y->size) {
    return -1;
  } if (x->size > y->size) {
    return 1;
  } else /* x->size == y->size */ {
    return memcmp(x->data, y->data, x->size);
  }
}

///////////////////////////////////////////////////////////////////////////
//
//  String wrapping.
//

void
pax_str_init(pax_str_t *str, const char *data, size_t size)
{
  str->data = g_memdup(data, size);
  str->size = size;
}

void
pax_str_reset(pax_str_t *str)
{
  g_free((void *)str->data);
  str->data = NULL;
  str->size = 0;
}

pax_str_t *
pax_str_new(const char *data, size_t size) {
  pax_str_t *str;

  str = g_malloc(sizeof(*str));
  pax_str_init(str, data, size);

  return str;
}

void pax_str_destroy(pax_str_t *str) {
  pax_str_reset(str);
  g_free(str);
}

///////////////////////////////////////////////////////////////////////////
//
//  UUID helpers.
//

void
pax_uuid_gen(pax_uuid_t *uuid)
{
  int urandom = open("/dev/urandom", O_RDONLY);
  read(urandom, uuid, sizeof(*uuid));
  close(urandom);
}

void
pax_uuid_destroy(pax_uuid_t *uuid)
{
  (void)uuid;
  return;
}

int
pax_uuid_compare(pax_uuid_t *x, pax_uuid_t *y)
{
  if (*x < *y) {
    return -1;
  } else if (*x > *y) {
    return 1;
  } else {
    return 0;
  }
}

///////////////////////////////////////////////////////////////////////////
//
//  Artificial msgpack primitives.
//

void
msgpack_pack_paxid(msgpack_packer *pk, paxid_t paxid)
{
  msgpack_pack_uint32(pk, paxid);
}

void
msgpack_pack_pax_uuid(msgpack_packer *pk, pax_uuid_t uuid)
{
  msgpack_pack_uint64(pk, uuid);
}

///////////////////////////////////////////////////////////////////////////
//
//  Msgpack helpers.
//

void
paxos_paxid_pack(struct paxos_yak *py, paxid_t paxid)
{
  msgpack_pack_paxid(py->pk, paxid);
}

void
paxos_paxid_unpack(paxid_t *paxid, msgpack_object *o)
{
  assert(o->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  *paxid = o->via.u64;
}

void
paxos_uuid_pack(struct paxos_yak *py, pax_uuid_t *uuid)
{
  msgpack_pack_pax_uuid(py->pk, *uuid);
}

void
paxos_uuid_unpack(pax_uuid_t *uuid, msgpack_object *o)
{
  assert(o->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  *uuid = o->via.u64;
}
