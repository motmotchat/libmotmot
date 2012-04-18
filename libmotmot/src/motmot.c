/**
 * motmot.c - libmotmot API
 */

#include "motmot.h"
#include "paxos.h"
#include "list.h"

#include <glib.h>

// Private data structures
struct motmot_callback_info {
  motmot_callback mc_callback;
  void *mc_data;
  LIST_ENTRY(motmot_callback_info) mc_le;
};
static LIST_HEAD(, motmot_callback_info) callback_list;

/**
 * motmot_send - queue the message for reliable ordered broadcast
 */
int
motmot_send(const char *message, size_t len)
{
  return paxos_request(DEC_CHAT, message, len);
}
/**
 * motmot_add_callback - add a callback that will be called upon the receipt of
 * every message. Doesn't check for duplicates, so "don't do that."
 */
int
motmot_add_callback(motmot_callback fn, void *data)
{
  struct motmot_callback_info *cb;

  cb = g_malloc0(sizeof(*cb));
  cb->mc_callback = fn;
  cb->mc_data = data;
  LIST_INSERT_TAIL(&callback_list, cb, mc_le);

  return 0;
}
/**
 * motmot_remove_callback - remove the given callback.
 *
 * Returns 0 if an element was successfully removed in this way, 1 otherwise.
 */
int
motmot_remove_callback(motmot_callback fn, void *data)
{
  struct motmot_callback_info *it;

  LIST_FOREACH(it, &callback_list, mc_le) {
    if (it->mc_callback == fn && it->mc_data == data) {
      LIST_REMOVE(&callback_list, it, mc_le);
      g_free(it);
      return 0;
    }
  }
  return 1;
}
