/**
 * paxos_print.h - Paxos object printers for debugging.
 */
#ifndef __PAXOS_PRINT_H__
#define __PAXOS_PRINT_H__

#include "paxos.h"

void paxid_print(paxid_t, const char *, const char *);
void ppair_print(ppair_t, const char *, const char *);
void paxop_print(paxop_t, const char *, const char *);
void dkind_print(dkind_t, const char *, const char *);

void paxos_header_print(struct paxos_header *, const char *, const char *);
void paxos_value_print(struct paxos_value *, const char *, const char *);
void paxos_acceptor_print(struct paxos_acceptor *, const char *, const char *);
void paxos_instance_print(struct paxos_instance *, const char *, const char *);
void paxos_request_print(struct paxos_request *, const char *, const char *);

#endif /* __PAXOS_PRINT_H__ */
