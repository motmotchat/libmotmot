/**
 * connect.h - Type representing a Paxos connection.
 */
#ifndef __PAXOS_TYPES_CONNECT_H__
#define __PAXOS_TYPES_CONNECT_H__

#include "containers/hashtable_factory.h"
#include "types/primitives.h"
#include "types/core.h"
#include "util/paxos_io.h"

/* Connection to another client; shared among sessions. */
struct paxos_connect {
  struct paxos_peer *pc_peer;         // wrapper around I/O channel
  pax_str_t pc_alias;                 // string identifying the client
  unsigned pc_refs;                   // number of references
  bool pc_pending;                    // pending reconnection?
};

HASHTABLE_DECLARE(connect);

struct paxos_connect *connect_new(const char *, size_t);
void connect_deref(struct paxos_connect **);

/* Paxos connection GLib hashtable utilities. */
void connect_hashinit(void);
unsigned connect_key_hash(const void *);
int connect_key_equals(const void *, const void *);

/* Msgpack helpers. */
void paxos_connect_pack(struct paxos_yak *, struct paxos_connect *);
void paxos_connect_unpack(struct paxos_connect *, msgpack_object *);

#endif /* __PAXOS_TYPES_CONNECT_H__ */
