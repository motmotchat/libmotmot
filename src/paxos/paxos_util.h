/**
 * paxos_util.h - Paxos protocol utilities which make use of session state.
 */
#ifndef __PAXOS_UTIL_H__
#define __PAXOS_UTIL_H__

#include "paxos.h"
#include "util/paxos_msgpack.h"

/* Error handling. */
#define ERR_RET(r, cmd)   \
  if (((r) = (cmd))) {    \
    return (r);           \
  }
#define ERR_ACCUM(r, cmd) \
  (r) = (r) | (cmd);

/* Convenience functions. */
inline int is_proposer(void);
inline void reset_proposer(void);
inline paxid_t next_instance(void);
inline int request_needs_cached(dkind_t dkind);
unsigned majority(void);

/* Protocol utilities. */
void instance_insert_and_upstart(struct paxos_instance *);
int paxos_broadcast_instance(struct paxos_instance *);
int proposer_decree_part(struct paxos_acceptor *, int force);

/* Message delivery I/O wrappers. */
int paxos_send(struct paxos_acceptor *, struct paxos_yak *);
int paxos_send_to_proposer(struct paxos_yak *);
int paxos_broadcast(struct paxos_yak *);

#endif /* __PAXOS_UTIL_H__ */
