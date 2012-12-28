/**
 * paxos_io.h - Paxos reliable IO utilities.
 */
#ifndef __PAXOS_IO_H__
#define __PAXOS_IO_H__

#include <glib.h>
#include <msgpack.h>

struct paxos_peer;

struct paxos_peer *paxos_peer_init(GIOChannel *);
void paxos_peer_destroy(struct paxos_peer *);
int paxos_peer_send(struct paxos_peer *, const char *, size_t);

#endif /* __PAXOS_IO_H__ */
