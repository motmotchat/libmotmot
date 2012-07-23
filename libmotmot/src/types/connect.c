/**
 * connect.c - Utilities for Paxos connections.
 */

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <murmurhash3.h>

#include "containers/hashtable_factory.h"
#include "types/connect.h"

uint32_t murmurseed;

HASHTABLE_IMPLEMENT(connect, pc_alias, connect_key_hash,
    connect_key_equals, _INL);

struct paxos_connect *
connect_new(const char *alias, size_t size)
{
  struct paxos_connect *conn;

  conn = g_malloc0(sizeof(*conn));
  conn->pc_alias.size = size;
  conn->pc_alias.data = g_memdup(alias, size);

  return conn;
}

static void
connect_destroy(struct paxos_connect *conn)
{
  g_free((void *)conn->pc_alias.data);
  paxos_peer_destroy(conn->pc_peer);
  g_free(conn);
}

void
connect_deref(struct paxos_connect **conn)
{
  if (--((*conn)->pc_refs) == 0) {
    connect_destroy(*conn);
  }
  *conn = NULL;
}

///////////////////////////////////////////////////////////////////////////
//
//  Hashtable callbacks.
//

/**
 * Initialize Murmurhash.
 */
void
connect_hashinit()
{
  int urandom = open("/dev/urandom", O_RDONLY);
  read(urandom, &murmurseed, sizeof(murmurseed));
  close(urandom);
}

/**
 * Hash a paxos_connect identity descriptor, i.e., a string.
 */
unsigned
connect_key_hash(const void *data)
{
  uint32_t r;
  pax_str_t *str = (pax_str_t *)data;

  MurmurHash3_x86_32(str->data, str->size, murmurseed, &r);
  return r;
}

/**
 * Check two paxos_connect keys, i.e., strings, for equality.
 */
int
connect_key_equals(const void *x, const void *y)
{
  return !pax_str_compare((pax_str_t *)x, (pax_str_t *)y);
}

///////////////////////////////////////////////////////////////////////////
//
//  Msgpack helpers.
//

void
paxos_connect_pack(struct paxos_yak *py, struct paxos_connect *conn)
{
  msgpack_pack_array(py->pk, 1);
  msgpack_pack_raw(py->pk, conn->pc_alias.size);
  msgpack_pack_raw_body(py->pk, conn->pc_alias.data, conn->pc_alias.size);
}

void
paxos_connect_unpack(struct paxos_connect *conn, msgpack_object *o)
{
  msgpack_object *p;

  // Make sure the input is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 1);

  conn->pc_peer = NULL;
  conn->pc_refs = 0;
  conn->pc_pending = false;

  p = o->via.array.ptr;
  assert(p->type == MSGPACK_OBJECT_RAW);
  conn->pc_alias.size = p->via.raw.size;
  conn->pc_alias.data = g_memdup(p->via.raw.ptr, p->via.raw.size);
}
