/**
 * msgpack_io.c - Buffered msgpack network IO.
 */

#include <glib.h>
#include <msgpack.h>

#include "common/msgpack_io.h"

#define BUFSIZE 4096

struct msgpack_conn {
  GIOChannel *mc_channel;
  msgpack_unpacker mc_unpacker;
  GString *mc_write_buffer;

  msgpack_conn_recv_t mc_recv;
  msgpack_conn_drop_t mc_drop;
};

int msgpack_conn_read(GIOChannel *, GIOCondition, void *);
int msgpack_conn_write(GIOChannel *, GIOCondition, void *);

/**
 * msgpack_conn_new - Set up read/write buffering.
 */
struct msgpack_conn *
msgpack_conn_new(GIOChannel *channel, msgpack_conn_recv_t recv,
    msgpack_conn_drop_t drop)
{
  struct msgpack_conn *conn;

  if (channel == NULL) {
    return NULL;
  }

  conn = g_malloc0(sizeof(*conn));
  conn->mc_channel = channel;
  conn->mc_recv = recv;
  conn->mc_drop = drop;

  // TODO: This needs to be done when the socket is first opened.
  // Put the conn's channel into nonblocking mode.
  g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL);

  // Set the channel encoding to binary.
  g_io_channel_set_encoding(channel, NULL, NULL);

  // Set up the read listener.
  msgpack_unpacker_init(&conn->mc_unpacker, BUFSIZE);
  g_io_add_watch(channel, G_IO_IN, msgpack_conn_read, conn);

  // Set up the write buffer.
  conn->mc_write_buffer = g_string_sized_new(BUFSIZE);

  return conn;
}

/**
 * msgpack_conn_destroy - Free up a conn.
 */
void
msgpack_conn_destroy(struct msgpack_conn *conn)
{
  GIOStatus status;
  GError *error = NULL;

  if (conn == NULL) {
    return;
  }

  // Clean up the msgpack unpacker/read buffer.
  msgpack_unpacker_destroy(&conn->mc_unpacker);

  // Get rid of our event listeners.
  while (g_source_remove_by_user_data(conn));

  // Flush and destroy the GIOChannel.
  status = g_io_channel_shutdown(conn->mc_channel, TRUE, &error);
  if (status != G_IO_STATUS_NORMAL) {
    g_warning("msgpack_conn_destroy: Trouble destroying conn.");
  }

  // Free the conn structure itself.
  g_free(conn);
}

/**
 * msgpack_conn_read - Buffer data from a socket read and deserialize.
 */
int
msgpack_conn_read(GIOChannel *channel, GIOCondition condition, void *data)
{
  struct msgpack_conn *conn;
  msgpack_unpacked result;
  size_t bytes_read;
  int r = TRUE;

  GIOStatus status;
  GError *error = NULL;

  conn = (struct msgpack_conn *)data;

  msgpack_unpacked_init(&result);

  // Keep reading until we've flushed the channel's read buffer completely.
  do {
    // Reserve enough space in the msgpack_unpacker buffer for a read.
    msgpack_unpacker_reserve_buffer(&conn->mc_unpacker, BUFSIZE);

    // Read up to BUFSIZE bytes into the stream.
    status = g_io_channel_read_chars(channel,
        msgpack_unpacker_buffer(&conn->mc_unpacker), BUFSIZE, &bytes_read,
        &error);

    if (status == G_IO_STATUS_ERROR) {
      g_warning("msgpack_conn_read: Read from socket failed.");
    }

    // Inform the msgpack_unpacker how much of the buffer we actually consumed.
    msgpack_unpacker_buffer_consumed(&conn->mc_unpacker, bytes_read);

    // Pop as many msgpack objects as we can get our hands on.
    while (msgpack_unpacker_next(&conn->mc_unpacker, &result)) {
      if (conn->mc_recv(conn, &result.data)) {
        g_warning("paxos_read_conn: Dispatch failed.");
        r = FALSE;
        break;
      }
    }

    // Drop the connection if we're at the end.
    if (status == G_IO_STATUS_EOF) {
      conn->mc_drop(conn);
      r = FALSE;
      break;
    }

  } while (g_io_channel_get_buffer_condition(channel) & G_IO_IN);

  msgpack_unpacked_destroy(&result);
  return r;
}

/**
 * msgpack_conn_write - Write data reliably to a connection.
 */
int
msgpack_conn_write(GIOChannel *channel, GIOCondition condition, void *data)
{
  struct msgpack_conn *conn;
  size_t bytes_written;

  GIOStatus status;
  GError *error = NULL;

  conn = (struct msgpack_conn *)data;

  // If there's nothing to write, do nothing.
  if (conn->mc_write_buffer->len == 0) {
    return TRUE;
  }

  // Write to the channel.
  status = g_io_channel_write_chars(channel, conn->mc_write_buffer->str,
      conn->mc_write_buffer->len, &bytes_written, &error);

  if (status == G_IO_STATUS_ERROR) {
    g_warning("msgpack_conn_write: Write to socket failed.");
  }

  g_string_erase(conn->mc_write_buffer, 0, bytes_written);

  if (conn->mc_write_buffer->len == 0) {
    // XXX: This is kind of hax.
    while (g_source_remove_by_user_data(conn));
    g_io_add_watch(conn->mc_channel, G_IO_IN, msgpack_conn_read, conn);
  }

  // Flush the channel.
  error = NULL;
  if (g_io_channel_flush(conn->mc_channel, &error) == G_IO_STATUS_ERROR) {
    g_critical("msgpack_conn_write: Could not flush channel.");
  }

  if (status == G_IO_STATUS_EOF) {
    // Flush the read buffer.  We will also detect the EOF in msgpack_conn_read,
    // which will destroy the conn for us.
    if (g_io_channel_get_buffer_condition(channel) & G_IO_IN) {
      msgpack_conn_read(channel, G_IO_IN, data);
    }
    return FALSE;
  }

  return TRUE;
}

/**
 * msgpack_conn_send - Send the contents of a buffer to a conn.
 */
int
msgpack_conn_send(struct msgpack_conn *conn, const char *buffer, size_t length)
{
  // If there was no data in the buffer to begin with, it means we weren't
  // subscribed to write events.  Since we're populating the buffer now, let's
  // start listening.
  if (conn->mc_write_buffer->len == 0 && length > 0) {
    g_io_add_watch(conn->mc_channel, G_IO_OUT, msgpack_conn_write, conn);
  }

  g_string_append_len(conn->mc_write_buffer, buffer, length);

  return 0;
}
