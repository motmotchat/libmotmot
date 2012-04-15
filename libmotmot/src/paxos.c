/**
 * paxos.c - Implementation of the Paxos consensus protocol
 */
#include "paxos.h"
#include "paxos_msgpack.h"
#include "list.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <glib.h>

#define MPBUFSIZE 4096

// Local protocol state
struct paxos_state pax;

// Paxos operations
void paxos_drop_connection(GIOChannel *);

// Proposer operations
int proposer_prepare(GIOChannel *);
int proposer_ack_promise(struct paxos_hdr *, msgpack_object *);

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
  int retval;
  struct paxos_hdr *hdr;

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
    paxos_drop_connection(source);
    return FALSE;
  }

  msgpack_unpacker_buffer_consumed(pac, len);

  // Pop a single Paxos payload.
  msgpack_unpacked_init(&res);
  msgpack_unpacker_next(pac, &res);
  o = res.data;

  // TODO: error handling?
  assert(o.type == MSGPACK_OBJECT_ARRAY && o.via.array.size > 0 &&
      o.via.array.size <= 2);

  p = o.via.array.ptr;

  // Unpack the Paxos header.
  hdr = g_malloc(sizeof(struct paxos_hdr));
  if (hdr == NULL) {
    // TODO: error handling
    dprintf(2, "paxos_dispatch: Could not allocate header.\n");
  }
  paxos_hdr_unpack(hdr, p);

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

/**
 * paxos_drop_connection - Account for a lost connection.
 *
 * We mark the acceptor as unavailable, "elect" the new president locally,
 * and start a prepare phase if necessary.
 */
void
paxos_drop_connection(GIOChannel *source)
{
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
    // Let's mark the new one.
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
}

/**
 * proposer_prepare - Broadcast a prepare message to all acceptors.
 *
 * The initiation of a prepare sequence is only allowed if we believe
 * ourselves to be the proposer.  Moreover, each proposer needs to make it
 * exactly one time.  Therefore, we call proposer_prepare() when and only
 * when:
 *  - We just lost the connection to the previous proposer.
 *  - We were next in line to be proposer.
 */
int
proposer_prepare(GIOChannel *source)
{
  struct paxos_hdr hdr;
  struct paxos_decree *dec;
  struct paxos_yak py;

  // If we were already preparing, get rid of that prepare.
  // XXX: I don't think this is possible.
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

  // Pack a prepare and broadcast it.
  paxos_payload_new(&py, 1);
  paxos_hdr_pack(&py, &hdr);
  paxos_broadcast(paxos_payload_data(&py), paxos_payload_size(&py));

  return 0;
}

/**
 * proposer_ack_promise - Acknowledge an acceptor's promise.
 *
 * We acknowledge the promise by incrementing the number of acceptors who
 * have responded to the prepare, and by accounting for the acceptor's
 * votes on those decrees for which we indicated we did not have commit
 * information.
 *
 * If we attain a majority of promises, we make decrees for all those
 * instances in which any acceptor voted, as well as null decrees for
 * any holes.  We then end the prepare.
 */
int
proposer_ack_promise(struct paxos_hdr *hdr, msgpack_object *o)
{
  paxid_t acc_id;
  msgpack_object *p, *pend, *r;
  struct paxos_decree *it, *dec;
  struct decree_list *prep_dlist;

  // If the prepare is for some other ballot, ignore it.
  if (!ballot_compare(pax.ballot, hdr->ph_ballot)) {
    return 0;
  }

  // Grab the acceptor's ID.
  p = o->via.array.ptr;
  acc_id = p[0].via.u64;

  // Initialize loop variables.
  pend = p[1].via.array.ptr + p[1].via.array.size;
  p = p[1].via.array.ptr;
  prep_dlist = &(pax.prep->pp_dlist);
  it = LIST_FIRST(prep_dlist);

  // Loop through all the past vote information.
  for (; p != pend; ++p) {
    r = p->via.array.ptr;

    // Extract a decree.
    dec = g_malloc(sizeof(struct paxos_decree));
    if (dec == NULL) {
      // TODO: cry
    }
    paxos_hdr_unpack(&dec->pd_hdr, r);
    paxos_val_unpack(&dec->pd_val, r + 1);

    // Find a decree for the same Paxos instance if one exists in the prep
    // list; otherwise, get the next highest instance.
    for (; it != (void *)prep_dlist; it = it->pd_le.le_next) {
      if (dec->pd_hdr.ph_inst <= it->pd_hdr.ph_inst) {
        break;
      }
    }

    // If the instance was not found, insert it at the tail or before the
    // iterator as necessary.  If it was found, compare the two, keeping or
    // inserting the larger-balloted decree in the list and freeing the other.
    if (it == (void *)prep_dlist) {
      LIST_INSERT_TAIL(prep_dlist, dec, pd_le);
    } else if (dec->pd_hdr.ph_inst < it->pd_hdr.ph_inst) {
      LIST_INSERT_BEFORE(prep_dlist, it, dec, pd_le);
    } else /* dec->pd_hdr.ph_inst == it->pd_hdr.ph_inst */ {
      if (ballot_compare(dec->pd_hdr.ph_ballot, it->pd_hdr.ph_ballot) > 1) {
        LIST_INSERT_BEFORE(prep_dlist, it, dec, pd_le);
        LIST_REMOVE(prep_dlist, it, pd_le);
        decree_free(it);
      } else {
        decree_free(dec);
      }
    }
  }

  // Acknowledge the prep.
  pax.prep->pp_nacks++;

  // Return if we don't have a majority of acks; otherwise, end the prepare.
  if (pax.prep->pp_nacks < MAJORITY) {
    return 0;
  }

  // TODO: Process the list.

  // Free the prepare and return.
  g_free(pax.prep);
  return 0;
}
