/**
 * paxos_continue.h - Boilerplate macro for continuation-like callbacks which
 * are invoked by the client once GIOChannel connections are established.
 */

#define CONNECTINUATE(op)                                       \
  int                                                           \
  continue_##op(GIOChannel *chan, void *data)                   \
  {                                                             \
    int r = 0;                                                  \
    struct paxos_continuation *k;                               \
    struct paxos_acceptor *acc;                                 \
                                                                \
    k = data;                                                   \
    pax = session_find(&state.sessions, k->pk_session_id);      \
    if (pax == NULL) {                                          \
      return 0;                                                 \
    }                                                           \
                                                                \
    /* Obtain the acceptor.  Only do the continue if the  */    \
    /* acceptor has not been parted in the meantime.      */    \
    acc = acceptor_find(&pax->alist, k->pk_paxid);              \
    if (acc != NULL) {                                          \
      r = do_continue_##op(chan, acc, k);                       \
    }                                                           \
                                                                \
    LIST_REMOVE(&pax->clist, k, pk_le);                         \
    continuation_destroy(k);                                    \
                                                                \
    return r;                                                   \
  }
