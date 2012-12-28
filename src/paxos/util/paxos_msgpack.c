/**
 * paxos_msgpack.c - Paxos yak wrapper utilities for msgpack.
 */

#include <assert.h>
#include <msgpack.h>

#include "util/paxos_msgpack.h"

void
paxos_payload_init(struct paxos_yak *py, size_t n)
{
  py->buf = msgpack_sbuffer_new();
  py->pk = msgpack_packer_new(py->buf, msgpack_sbuffer_write);

  msgpack_pack_array(py->pk, n);
}

void
paxos_payload_begin_array(struct paxos_yak *py, size_t n)
{
  msgpack_pack_array(py->pk, n);
}

void
paxos_payload_destroy(struct paxos_yak *py)
{
  msgpack_packer_free(py->pk);
  msgpack_sbuffer_free(py->buf);

  py->pk = NULL;
  py->buf = NULL;
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
