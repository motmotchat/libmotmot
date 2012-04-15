/**
 * paxos.c - Implementation of the Paxos consensus protocol
 */
#include "paxos.h"
#include "paxos_msgpack.h"
#include "list.h"

#include <assert.h>
#include <glib.h>

#define MPBUFSIZE 4096

// Local protocol state
struct paxos_state pax;

// Prototypes
int paxos_prepare(GIOChannel *);

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
    struct paxos_acceptor *it;

    // Connection dropped; mark the acceptor as dead.
    LIST_FOREACH(it, &(pax.alist), pa_le) {
      if (it->pa_chan == source) {
        it->pa_chan = NULL;
        break;
      }
    }

    // Oh noes!  Did we lose the proposer?
    if (it->pa_paxid == pax.proposer->pa_paxid) {
      // Yup; let's find the new one.
      LIST_FOREACH(it, &(pax.alist), pa_le) {
        if (it->pa_chan != NULL) {
          pax.proposer = it;
          break;
        }
      }

      // If we're the new proposer, send a prepare.
      if (is_proposer()) {
        paxos_prepare(source);
      }
    }

    // Close the channel socket.
    close(gio_channel_unix_get_fd(source));

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

/*
 * paxos_prepare - Broadcast a prepare message to all acceptors.
 *
 * The initiation of a prepare sequence is only allowed if we believe
 * ourselves to be the proposer.  Moreover, each proposer needs to make it
 * exactly one time.  Therefore, we call paxos_prepare() when and only when:
 *  - We just dropped the connection to the previous proposer.
 *  - We were the next proposer in line.
 */
int
paxos_prepare(GIOChannel *source)
{
  struct paxos_hdr hdr;
  struct paxos_decree *dec;
  struct paxos_yak py;
  struct paxos_acceptor *acc;

  GError *gerr;
  GIOStatus status;
  unsigned long len;

  // If we were already preparing, get rid of that prepare.
  if (pax.prep != NULL) {
    g_free(pax.prep);
  }

  // Start a new prepare and a new ballot.
  pax.prep = g_malloc(sizeof(struct paxos_prep));
  if (pax.prep == NULL) {
    // TODO: error handling
    return -1;
  }
  pax.ballot.id = pax.self_id;
  pax.ballot.gen++;

  // Initialize a Paxos header.
  hdr.ph_ballot = pax.ballot;
  hdr.ph_opcode = OP_PREPARE;
  LIST_FOREACH(dec, &(pax.dlist), pd_le) {
    if (dec->pd_votes != 0) {
      hdr.ph_inst = dec->pd_hdr.ph_inst;
    }
  }

  // Pack a prepare.
  paxos_payload_new(&py, 1);
  paxos_hdr_pack(&py, &hdr);

  // Broadcast it.
  LIST_FOREACH(acc, &(pax.alist), pa_le) {
    if (acc->pa_chan == NULL) {
      continue;
    }

    gerr = NULL;
    status = g_io_channel_write_chars(
        acc->pa_chan,
        paxos_payload_data(&py),
        paxos_payload_size(&py),
        &len,
        &gerr
    );
    if (status == G_IO_STATUS_ERROR) {
      // TODO: error handling
      dprintf(2, "paxos_prepare: Could not write prepare to socket.\n");
    }

    gerr = NULL;
    status = g_io_channel_flush(acc->pa_chan, &gerr);
    if (status == G_IO_STATUS_ERROR) {
      // TODO: error handling
      dprintf(2, "paxos_prepare: Could not flush prepare on socket.\n");
    }
  }
}
