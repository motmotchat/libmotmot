/**
 * paxos_io.h - Paxos reliable IO utilities.
 */
#ifndef __PAXOS_IO_H__
#define __PAXOS_IO_H__

#include <glib.h>
#include <msgpack.h>

// Clients of paxos_io.h should treat this struct as opaque.
struct paxos_peer {
  GIOChannel *pp_channel;         // channel to the peer
  int pp_read, pp_write;          // GLib event source IDs
  msgpack_unpacker pp_unpacker;   // unpacker (and its associated read buffer)
  struct {
    char *data;
    size_t length;
  } pp_write_buffer;              // write buffer (for unwritten data)
                                  // TODO: make this something more efficient
};

struct paxos_peer *paxos_peer_init(GIOChannel *);
void paxos_peer_destroy(struct paxos_peer *);
int paxos_peer_send(struct paxos_peer *, const char *, size_t);

#endif /* __PAXOS_IO_H__ */
