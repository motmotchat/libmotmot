/**
 * paxos_helper.h - Paxos protocol helper routines.
 */
#ifndef __PAXOS_HELPER_H__
#define __PAXOS_HELPER_H__

#include "paxos.h"

#include <glib.h>
#include <msgpack.h>

/* Error handling. */
#define ERR_RET(r, cmd)   \
  if ((r = cmd)) {        \
    return r;             \
  }

/* Convenience functions. */
inline int is_proposer(void);
inline void reset_proposer(void);
inline paxid_t next_instance(void);
inline int request_needs_cached(dkind_t dkind);
unsigned majority(void);

/* Paxid pair comparison functions. */
int ppair_compare(ppair_t, ppair_t);
int ballot_compare(ballot_t, ballot_t);
int reqid_compare(reqid_t, reqid_t);

/* Initializers and destructors. */
void instance_reset_metadata(struct paxos_instance *);
void header_init(struct paxos_header *, paxop_t, paxid_t);
void acceptor_destroy(struct paxos_acceptor *);
void instance_destroy(struct paxos_instance *);
void request_destroy(struct paxos_request *);

/* List helpers. */
struct paxos_acceptor *acceptor_find(struct acceptor_list *, paxid_t);
struct paxos_instance *instance_find(struct instance_list *, paxid_t);
struct paxos_request *request_find(struct request_list *, reqid_t);

struct paxos_acceptor *acceptor_insert(struct acceptor_list *,
    struct paxos_acceptor *);
struct paxos_instance *instance_insert(struct instance_list *,
    struct paxos_instance *);
struct paxos_request *request_insert(struct request_list *,
    struct paxos_request *);

void acceptor_list_destroy(struct acceptor_list *);
void instance_list_destroy(struct instance_list *);
void request_list_destroy(struct request_list *);

/* Paxos message sending. */
int paxos_broadcast(const char *, size_t);
int paxos_send(struct paxos_acceptor *, const char *, size_t);
int paxos_send_to_proposer(const char *, size_t);

#endif /* __PAXOS_HELPER_H__ */
