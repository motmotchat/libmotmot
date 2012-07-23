/**
 * connect.c - Utilities for Paxos connections.
 */

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

void
connect_destroy(struct paxos_connect *conn)
{
  g_free((void *)conn->pc_alias.data);
  paxos_peer_destroy(conn->pc_peer);
  g_free(conn);
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
