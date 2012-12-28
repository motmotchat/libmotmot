/**
 * yakyak.c - Wrapper utilities for msgpack payloads.
 */

#include <msgpack.h>

#include "common/yakyak.h"

void
yakyak_init(struct yakyak *yy, size_t n)
{
  yy->buf = msgpack_sbuffer_new();
  yy->pk = msgpack_packer_new(yy->buf, msgpack_sbuffer_write);

  msgpack_pack_array(yy->pk, n);
}

void
yakyak_destroy(struct yakyak *yy)
{
  msgpack_packer_free(yy->pk);
  msgpack_sbuffer_free(yy->buf);

  yy->pk = NULL;
  yy->buf = NULL;
}

void
yakyak_begin_array(struct yakyak *yy, size_t n)
{
  msgpack_pack_array(yy->pk, n);
}

char *
yakyak_data(struct yakyak *yy)
{
  return yy->buf->data;
}

size_t
yakyak_size(struct yakyak *yy)
{
  return yy->buf->size;
}
