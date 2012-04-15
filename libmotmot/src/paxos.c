/**
 * paxos.c - Implementation of the Paxos consensus protocol
 */
#include "paxos.h"
#include "paxos_msgpack.h"
#include "list.h"

#include <assert.h>
#include <unistd.h>
#include <glib.h>

#define MPBUFSIZE 4096

// Local protocol state
struct paxos_state pax;

// Proposer operations
int proposer_prepare(GIOChannel *);
void proposer_ack_promise(struct paxos_hdr *, msgpack_object *);

// Acceptor operations

int
proposer_dispatch(GIOChannel *source, struct paxos_hdr *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      // TODO: paxos_redirect();
      break;
    case OP_PROMISE:
      proposer_ack_promise(hdr, o);
      break;

    case OP_DECREE:
      // TODO: paxos_redirect();
      break;

    case OP_ACCEPT:
      break;

    case OP_COMMIT:
      // TODO: decide what to do
      break;

    case OP_REQUEST:
      break;

    case OP_REDIRECT:
      // TODO: decide what to do
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
  msgpack_object o, *p;

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
        proposer_prepare(source);
      }
    }

    // Close the channel socket.
    close(g_io_channel_unix_get_fd(source));

    return FALSE;
  }

  msgpack_unpacker_buffer_consumed(pac, len);

  // Pop a single Paxos payload.
  msgpack_unpacked_init(&res);
  msgpack_unpacker_next(pac, &res);
  o = res.data;

  // TODO: error handling?
  assert(o.type == MSGPACK_OBJECT_ARRAY && o.via.array.size > 0
      && o.via.array.size <= 2);

  p = o.via.array.ptr;

  // Unpack the Paxos header.
  hdr = g_malloc(sizeof(struct paxos_hdr));
  if (hdr == NULL) {
    // TODO: cry
  }

  paxos_hdr_unpack(hdr, p);
  if (hdr == NULL) {
    // TODO: error handling
    dprintf(2, "paxos_dispatch: Could not unpack header.\n");
  }

  // Switch on the type of message received.
  if (is_proposer()) {
    retval = proposer_dispatch(source, hdr, p + 1);
  } else {
    retval = acceptor_dispatch(source, hdr, p + 1);
  }

  // TODO: freeing the msgpack_object

  g_free(hdr);
  return retval;
}

/*
 * proposer_prepare - Broadcast a prepare message to all acceptors.
 *
 * The initiation of a prepare sequence is only allowed if we believe
 * ourselves to be the proposer.  Moreover, each proposer needs to make it
 * exactly one time.  Therefore, we call proposer_prepare() when and only
 * when:
 *  - We just lost the connection to the previous proposer.
 *  - We were the next proposer in line.
 */
int
proposer_prepare(GIOChannel *source)
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
      dprintf(2, "proposer_prepare: Could not write prepare to socket.\n");
    }

    gerr = NULL;
    status = g_io_channel_flush(acc->pa_chan, &gerr);
    if (status == G_IO_STATUS_ERROR) {
      // TODO: error handling
      dprintf(2, "proposer_prepare: Could not flush prepare on socket.\n");
    }
  }

  return TRUE;
}

void
proposer_ack_promise(struct paxos_hdr *hdr, msgpack_object *o)
{
  paxid_t acc_id;
  msgpack_object *p, *pend, *r;
  struct paxos_decree *dec;

  // If the prepare is for some other ballot, ignore it.
  if (!ballot_compare(pax.ballot, hdr->ph_ballot)) {
    return;
  }

  // Grab the acceptor's ID.
  p = o->via.array.ptr;
  acc_id = p[0].via.u64;

  pend = p[1].via.array.ptr + p[1].via.array.size;

  // Extract all the past vote information.
  for (p = p[1].via.array.ptr; p != pend; ++p) {
    r = p->via.array.ptr;

    dec = g_malloc(sizeof(struct paxos_decree));
    if (dec == NULL) {
      // TODO: cry
    }

    paxos_hdr_unpack(&dec->pd_hdr, r);
    paxos_val_unpack(&dec->pd_val, r + 1);

    struct paxos_decree *dec0 = decree_find(&pax.prep->pp_dlist, hdr->ph_inst);
    (void)dec0;
    if (ballot_compare(dec->pd_hdr.ph_ballot, dec0->pd_hdr.ph_ballot) > 1) {
      // XXX: Replace the decree.
    }
  }

  // Acknowledge the prep.
  pax.prep->pp_nacks++;

  // Return if we don't have a majority of acks; otherwise, end the prepare.
  if (pax.prep->pp_nacks < MAJORITY) {
    return;
  }

  // XXX: Do things.
}
