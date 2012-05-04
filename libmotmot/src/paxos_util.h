/**
 * paxos_util.h - Paxos protocol utilities which make use of session state.
 */
#ifndef __PAXOS_UTIL_H__
#define __PAXOS_UTIL_H__

#include "paxos.h"

/* Convenience functions. */
inline int is_proposer(void);
inline void reset_proposer(void);
inline paxid_t next_instance(void);
inline int request_needs_cached(dkind_t dkind);
unsigned majority(void);

/* Protocol wrappers. */
void instance_insert_and_upstart(struct paxos_instance *);
int paxos_broadcast_instance(struct paxos_instance *);
int proposer_decree_part(struct paxos_acceptor *);

#endif /* __PAXOS_UTIL_H__ */
