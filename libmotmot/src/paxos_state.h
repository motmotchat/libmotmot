/**
 * paxos_state.h - Global state of the multi-session Paxos protocol.
 */

#include "paxos.h"

struct paxos_state {
  struct paxos_connect *self;         // null connection for ourselves

  connect_t connect;                  // callback for initiating connections
  enter_t enter;                      // callback for entering chat
  leave_t leave;                      // callback for leaving chat
  struct learn_table learn;           // callbacks for paxos_learn

  session_container sessions;         // list of active Paxos sessions
  connect_container *connections;     // hash table of connections
};

extern struct paxos_state state;

/**
 * This variable always references the current session from the point of view
 * of the current thread.  If we were multithreading, it would be thread-local,
 * but of course we aren't.
 */
extern struct paxos_session *pax;
