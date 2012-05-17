/**
 * msgpackutil.h - msgpack helper functions
 */

#ifndef __MSGPACKUTIL_H__
#define __MSGPACKUTIL_H__

#include <msgpack.h>
#include <string.h>

/**
 * A yak. Yaks carry things. Like messagepack objects. That you serialize.
 */
struct yak {
  msgpack_sbuffer   buf;
  msgpack_packer    pack;
};

#define yak_pack(yak, type, data) msgpack_pack_##type(&(yak)->pack, data)
#define yak_array(yak, len) msgpack_pack_array(&(yak)->pack, len)

static inline void
yak_init(struct yak *yak, size_t nelem)
{
  msgpack_sbuffer_init(&yak->buf);
  msgpack_packer_init(&yak->pack, &yak->buf, msgpack_sbuffer_write);
  yak_array(yak, nelem);
}

static inline void
yak_destroy(struct yak *yak)
{
  msgpack_sbuffer_destroy(&yak->buf);
}

static inline void
yak_string(struct yak *yak, const char *data, size_t size)
{
  msgpack_pack_raw(&yak->pack, size);
  msgpack_pack_raw_body(&yak->pack, data, size);
}

static inline void
yak_cstring(struct yak *yak, const char *str)
{
  yak_string(yak, str, strlen(str));
}

#define UNYAK(yak) (yak)->buf.data, (yak)->buf.size

#endif // __MSGPACKUTIL_H__
