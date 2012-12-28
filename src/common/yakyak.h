/**
 * yakyak.h - Wrapper utilities for msgpack payloads.
 */
#ifndef __MOTMOT_YAKYAK_H__
#define __MOTMOT_YAKYAK_H__

#include <msgpack.h>

/**
 * Large, wooly carrier of messages from the Himalayas who is best friends with
 * the motmot.  Clients of the yakyak should treat this struct as opaque, but
 * we expose it to allow for stack allocation.
 */
struct yakyak {
  msgpack_packer *pk;     // message packer
  msgpack_sbuffer *buf;   // associated read buffer; requires separate free
};

/* Paxos yak utilities. */
void yakyak_init(struct yakyak *, size_t);
void yakyak_destroy(struct yakyak *);
void yakyak_begin_array(struct yakyak *, size_t);
char *yakyak_data(struct yakyak *);
size_t yakyak_size(struct yakyak *);

#endif /* __MOTMOT_YAKYAK_H__ */
