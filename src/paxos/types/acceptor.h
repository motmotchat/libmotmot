/**
 * acceptor.h - Per-session representation of a chat participant.
 */
#ifndef __PAXOS_TYPES_ACCEPTOR_H__
#define __PAXOS_TYPES_ACCEPTOR_H__

#include "common/yakyak.h"

#include "containers/list_factory.h"
#include "types/primitives.h"

/* A Paxos protocol participant. */
struct paxos_acceptor {
  paxid_t pa_paxid;                   // instance number of the agent's JOIN
  struct paxos_connect *pa_conn;      // connection to acceptor
  LIST_ENTRY(paxos_acceptor) pa_le;   // sorted linked list of all participants
  // TODO: remove
  struct paxos_peer *pa_peer;
  size_t pa_size;
  void *pa_desc;
};

LIST_DECLARE(acceptor, paxid_t);
void acceptor_destroy(struct paxos_acceptor *);

/* Msgpack helpers. */
void paxos_acceptor_pack(struct yakyak *, struct paxos_acceptor *);
void paxos_acceptor_unpack(struct paxos_acceptor *, msgpack_object *);

#endif /* __PAXOS_TYPES_ACCEPTOR_H__ */
