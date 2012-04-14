/**
 * paxos.c - Implementation of the Paxos consensus protocol
 */
#include "paxos.h"
#include "list.h"

#include <assert.h>
#include <glib.h>
#include <msgpack.h>

#define MPBUFSIZE 4096

struct paxos_state pax;

int
proposer_dispatch(GIOChannel *source, struct paxos_hdr *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      break;

    case OP_PROMISE:
      break;

    case OP_DECREE:
      break;

    case OP_ACCEPT:
      break;

    case OP_COMMIT:
      break;

    case OP_REQUEST:
      break;

    case OP_REDIRECT:
      break;
  }

  return TRUE;
}

int
acceptor_dispatch(GIOChannel *source, struct paxos_hdr *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      break;

    case OP_PROMISE:
      break;

    case OP_DECREE:
      break;

    case OP_ACCEPT:
      break;

    case OP_COMMIT:
      break;

    case OP_REQUEST:
      break;

    case OP_REDIRECT:
      break;
  }

  return TRUE;
}

/*
 * Handle a Paxos message.
 *
 * XXX: We should probably separate the protocol work from the buffer motion.
 */
int
paxos_dispatch(GIOChannel *source, GIOCondition condition, void *data)
{
  struct paxos_hdr *hdr;
  int retval;

  msgpack_unpacker *pac;
  unsigned long len;
  msgpack_unpacked res;
  msgpack_object o, *p, *pend;

  GIOStatus status;
  GError *gerr = NULL;

  // Prep the msgpack stream.
  pac = (msgpack_unpacker *)data;
  msgpack_unpacker_reserve_buffer(pac, MPBUFSIZE);

  // Read up to MPBUFSIZE bytes into the stream.
  status = g_io_channel_read_chars(source, msgpack_unpacker_buffer(pac),
                                   MPBUFSIZE, &len, &gerr);
  if (status == G_IO_STATUS_ERROR) {
    // TODO: error handling
    dprintf(2, "paxos_dispatch: Read from socket failed.\n");
  } else if (status == G_IO_STATUS_EOF) {
    dprintf(2, "paxos_dispatch: Received disconnect.\n");
    return FALSE;
  }
  msgpack_unpacker_buffer_consumed(pac, len);

  // Pop a single Paxos payload.
  msgpack_unpacked_init(&res);
  msgpack_unpacker_next(pac, &res);
  o = res.data;

  // TODO: better error handling
  assert(o.type == MSGPACK_OBJECT_ARRAY && o.via.array.size > 0
      && o.via.array.size <= 2);

  p = o.via.array.ptr;

  // Unpack the Paxos header.
  hdr = paxos_hdr_unpack(p);
  if (hdr == NULL) {
    // TODO: error handling
    dprintf(2, "paxos_dispatch: Could not unpack header.\n");
  }
  ++p;

  // Switch on the type of message received.
  if (is_proposer()) {
    retval = proposer_dispatch(source, hdr, p);
  } else {
    retval = acceptor_dispatch(source, hdr, p);
  }

  // TODO: freeing the msgpack_object

  g_free(hdr);
  return retval;
}
