/**
 * paxos_io.c - Paxos reliable IO utilities
 */
#include "paxos.h"
#include "paxos_io.h"

#include <glib.h>

#define MPBUFSIZE 4096

// Private stuff.
int paxos_peer_read(GIOChannel *, GIOCondition, void *);
int paxos_peer_write(GIOChannel *, GIOCondition, void *);

/**
 * paxos_peer_init - Set up peer read/write buffering.
 */
struct paxos_peer *
paxos_peer_init(GIOChannel *channel)
{
  struct paxos_peer *peer;

  peer = g_malloc0(sizeof(*peer));
  peer->pp_channel = channel;

  // Put the peer's channel into nonblocking mode
  // TODO: error checking
  g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL);

  // Set up the read listener.
  msgpack_unpacker_init(&peer->pp_unpacker, MPBUFSIZE);
  g_io_add_watch(channel, G_IO_IN, paxos_peer_read, peer);

  return peer;
}

/**
 * paxos_peer_destroy - Free up a peer.
 */
void
paxos_peer_destroy(struct paxos_peer *peer)
{
  GIOStatus status;
  GError *error;

  if (peer == NULL) {
    return;
  }

  // Clean up the msgpack read buffer / unpacker.
  msgpack_unpacker_destroy(&peer->pp_unpacker);

  // Get rid of our event listeners.
  while (g_source_remove_by_user_data(peer));

  // Flush and destroy the GIOChannel.
  status = g_io_channel_shutdown(peer->pp_channel, TRUE, &error);
  if (status != G_IO_STATUS_NORMAL) {
    g_warning("paxos_peer_destroy: Trouble destroying peer.\n");
  }

  // Free the peer structure itself.
  g_free(peer);
}

/**
 * paxos_peer_read - Buffer data from a socket read and deserialize.
 */
int
paxos_peer_read(GIOChannel *channel, GIOCondition condition, void *data)
{
  struct paxos_peer *peer;
  msgpack_unpacked result;
  size_t bytes_read;
  int retval = TRUE;

  GIOStatus status;
  GError *error = NULL;

  peer = (struct paxos_peer *)data;

  // Reserve enough space in the msgpack_unpacker buffer for a read.
  msgpack_unpacker_reserve_buffer(&peer->pp_unpacker, MPBUFSIZE);

  // Read up to MPBUFSIZE bytes into the stream.
  status = g_io_channel_read_chars(channel,
      msgpack_unpacker_buffer(&peer->pp_unpacker), MPBUFSIZE, &bytes_read,
      &error);

  if (status == G_IO_STATUS_ERROR) {
    g_warning("paxos_peer_read: Read from socket failed.\n");
  }

  // Inform the msgpack_unpacker how much of the buffer we actually consumed.
  msgpack_unpacker_buffer_consumed(&peer->pp_unpacker, bytes_read);

  // Pop as many msgpack objects as we can get our hands on.
  // TODO: If we have a lot of data buffered but don't have any messages,
  // then something has gone terribly wrong and we should abort.
  msgpack_unpacked_init(&result);
  while (msgpack_unpacker_next(&peer->pp_unpacker, &result)) {
    if (paxos_dispatch(peer, &result.data) != 0) {
      g_warning("paxos_read_peer: Dispatch failed.\n");
      retval = FALSE;
      break;
    }
  }

  if (status == G_IO_STATUS_EOF) {
    paxos_drop_connection(peer);
    retval = FALSE;
  }

  msgpack_unpacked_destroy(&result);
  return retval;
}

/**
 * paxos_peer_write - Write data reliably to a peer.
 */
int
paxos_peer_write(GIOChannel *channel, GIOCondition condition, void *data)
{
  struct paxos_peer *peer = (struct paxos_peer *)data;
  size_t bytes_written;
  char *new_data;

  GIOStatus status;
  GError *error;

  // If there's nothing to write, do nothing
  if (peer->pp_write_buffer.length == 0) {
    return TRUE;
  }

  status = g_io_channel_write_chars(channel, peer->pp_write_buffer.data,
      peer->pp_write_buffer.length, &bytes_written, &error);

  if (status == G_IO_STATUS_ERROR) {
    g_warning("paxos_peer_write: Write to socket failed.\n");
  }

  // XXX: This is really awful.
  if (bytes_written == peer->pp_write_buffer.length) {
    peer->pp_write_buffer.length = 0;
    peer->pp_write_buffer.data = NULL;
    // XXX: this is kind of hax
    while (g_source_remove_by_user_data(peer));
    g_io_add_watch(peer->pp_channel, G_IO_IN, paxos_peer_read, peer);
  } else {
    peer->pp_write_buffer.length -= bytes_written;
    new_data = g_malloc(peer->pp_write_buffer.length);
    memcpy(new_data, peer->pp_write_buffer.data + bytes_written,
           peer->pp_write_buffer.length);
    g_free(peer->pp_write_buffer.data);
    peer->pp_write_buffer.data = new_data;
  }

  if (g_io_channel_flush(peer->pp_channel, &error) == G_IO_STATUS_ERROR) {
    g_critical("paxos_peer_write: Could not flush channel.\n");
  }

  if (status == G_IO_STATUS_EOF) {
    paxos_drop_connection(peer);
    return FALSE;
  }

  return TRUE;
}

/**
 * paxos_peer_send - Send the contents of a buffer to a peer.
 */
int
paxos_peer_send(struct paxos_peer *peer, const char *buffer, size_t length)
{
  char *new_data;

  if (length == 0) {
    return 0;
  }

  if (peer->pp_write_buffer.data == NULL) {
    // This means we weren't interested in the buffer, so we should add a
    // watch now to catch write events
    g_io_add_watch(peer->pp_channel, G_IO_OUT, paxos_peer_write, peer);
  }

  // XXX: Gross.
  new_data = g_malloc(peer->pp_write_buffer.length + length);
  if (peer->pp_write_buffer.data != NULL) {
    memcpy(new_data, peer->pp_write_buffer.data, peer->pp_write_buffer.length);
    g_free(peer->pp_write_buffer.data);
  }
  memcpy(new_data + peer->pp_write_buffer.length, buffer, length);

  peer->pp_write_buffer.data = new_data;
  peer->pp_write_buffer.length += length;

  return 0;
}
