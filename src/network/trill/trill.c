/**
 * trill.c - Trill public interface.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "trill/common.h"
#include "trill/crypto.h"
#include "util/socket.h"

/**
 * Probe message types, chosen to be non-colliding with TLS
 * ContentTypes http://tools.ietf.org/html/rfc2246#appendix-A.1.
 */
#define TRILL_NET_ACK   99
#define TRILL_NET_NOACK 100

/**
 * trill_init - Initialize Trill subservices.
 */
int
trill_init()
{
  if (!motmot_event_did_init()) {
    log_error("Failed to init Motmot event layer");
    return -1;
  }
  return trill_crypto_init();
}

/**
 * trill_connection_new - Instantiate a new Trill connection object.
 */
struct trill_connection *
trill_connection_new()
{
  struct trill_connection *conn;

  conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    return NULL;
  }

  // Create a nonblocking UDP socket.
  conn->tc_fd = socket_udp_nonblock(&conn->tc_port);
  if (conn->tc_fd == -1) {
    goto err;
  }

  // DTLS requires that we disable IP fragmentation.
  //
  // It appears that OS X doesn't support this without writing raw UDP packets
  // ourselves, and we can't do that.  "Oh well."
  //
  // http://lists.apple.com/archives/macnetworkprog/2006/Jul/msg00018.html
#if defined IP_DONTFRAG
  int optval = 1;
  setsockopt(conn->tc_fd, IPROTO_IP, IP_DONTFRAG, &optval, sizeof(optval));
#elif defined IP_MTU_DISCOVER
  int optval = IP_PMTUDISC_DO;
  setsockopt(conn->tc_fd, IPROTO_IP, IP_MTU_DISCOVER, &optval,
      sizeof(optval));
#endif

  // Initialize the TLS backend.
  if (trill_tls_init(conn)) {
    goto err;
  }

  conn->tc_state = TC_STATE_INIT;

  conn->tc_server_priority[0] = random();
  conn->tc_server_priority[1] = random();

  return conn;

err:
  trill_connection_free(conn);
  return NULL;
}

/**
 * trill_connection_free - Free up a connection object, closing the connection
 * if it's live.
 */
int
trill_connection_free(struct trill_connection *conn)
{
  int retval = 0;

  assert(conn != NULL && "Attempting to free a null connection");

  retval = trill_tls_free(conn);

  if (conn->tc_fd != -1 && close(conn->tc_fd) == -1) {
    log_errno("Error closing Trill connection socket");
    retval = -1;
  }

  free(conn->tc_remote_user);
  free(conn);

  return retval;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Connect protocol.
//

int trill_connection_probe(void *);
int trill_connection_read_probe(void *);

/**
 * trill_connected - Notify the client that trill_connect() has completed with
 * the given status code.
 */
void
trill_connected(struct trill_connection *conn, int status)
{
  if (conn->tc_connected_cb != NULL) {
    conn->tc_connected_cb(conn, status, conn->tc_data);
  }
}

/**
 * trill_connect - Begin connecting to a peer.
 *
 * We connect our open UDP socket with our peer and then enter the probing
 * stage of the connection protocol.
 */
int
trill_connect(struct trill_connection *conn, const char *who,
    const char *remote, uint16_t port)
{
  int ret;
  struct sockaddr_in addr;

  assert(conn != NULL && "Attempting to connect with a null connection");
  assert(remote != NULL && "Connecting to a null address");

  if (conn->tc_state != TC_STATE_INIT) {
    log_error("Connection is in an inappropriate state!");
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  ret = inet_pton(AF_INET, remote, &addr.sin_addr);
  if (ret == 0) {
    log_warn("Unparseable remote address %s", remote);
    return -1;
  } else if (ret == -1) {
    log_errno("System error while parsing remote address");
    return -1;
  }

  connect(conn->tc_fd, (struct sockaddr *) &addr, sizeof(addr));

  conn->tc_state = TC_STATE_PROBING;
  conn->tc_remote_user = strdup(who);

  // Begin probing.
  trill_want_read(conn, trill_connection_read_probe);
  motmot_event_want_timeout(trill_connection_probe, conn, conn->tc_data, 1000);

  return 0;
}

/**
 * trill_connection_probe - Send a probe message to our peer.
 *
 * We keep sending probes until either:
 * - We become the client; or
 * - We complete the handshake after becoming the server.
 */
int
trill_connection_probe(void *arg)
{
  struct trill_connection *conn;
  char buf[9];

  conn = (struct trill_connection *)arg;

  assert(conn->tc_state != TC_STATE_INIT);

  // Build the probe message. This looks like
  // +-----+---------------+--------------+
  // | ack | priority high | priority low |
  // +-----+---------------+--------------+
  // where ack is TRILL_NET_ACK if we've received a message from them, and
  // TRILL_NET_NOACK otherwise.
  //
  // We manually order the 32-bit priorities since there doesn't appear to be a
  // cross-platform 64-bit htonl-like function.
  if (conn->tc_state == TC_STATE_PROBING) {
    buf[0] = TRILL_NET_NOACK;
  } else if (conn->tc_state == TC_STATE_SERVER) {
    buf[0] = TRILL_NET_ACK;
  } else {
    return 0;
  }
  *(uint32_t *)(buf + 1) = htonl(conn->tc_server_priority[0]);
  *(uint32_t *)(buf + 5) = htonl(conn->tc_server_priority[1]);

  if (send(conn->tc_fd, buf, sizeof(buf), 0) < 0) {
    if (errno != EAGAIN && errno != EINTR) {
      log_errno("Error sending a probe");
    }
  }

  return 1;
}

/**
 * trill_connection_read_probe - Read a probe message from a peer and begin the
 * TLS handshake protocol if the client/server relationship has been settled.
 */
int
trill_connection_read_probe(void *arg)
{
  struct trill_connection *conn;
  char buf[10];
  uint32_t a, b;
  int len, winning;

  conn = (struct trill_connection *)arg;

  assert((conn->tc_state == TC_STATE_PROBING ||
      conn->tc_state == TC_STATE_CLIENT) &&
      "Bad state when reading probe");

  len = recv(conn->tc_fd, buf, sizeof(buf), 0);
  if (len == 9) {
    log_debug("Received a probe");
  } else if (len == 0) {
    log_errno("Error reading a probe");
    return 1;
  } else {
    log_warn("Discarding malformed probe");
    return 1;
  }

  a = ntohl(*(uint32_t *)(buf + 1));
  b = ntohl(*(uint32_t *)(buf + 5));

  // If in the unlikely event that our priority is tied with our peers, give up
  // and let the application retry.
  if (a == conn->tc_server_priority[0] && b == conn->tc_server_priority[1]) {
    trill_connected(conn, TRILL_ETIED);
    return 0;
  }

  // See if we've won the honor of being the server.
  winning = a > conn->tc_server_priority[0] ||
    (a == conn->tc_server_priority[0] && b > conn->tc_server_priority[1]);

  if (winning) {
    log_info("Probing done; we're the server");
    conn->tc_state = TC_STATE_SERVER;

    // Manually trigger the probe, since we have new data.
    trill_connection_probe(conn);

    // Start TLS handshaking.
    if (trill_start_tls(conn)) {
      trill_connected(conn, TRILL_ETLS);
    }
    return 0;
  } else if (!winning && buf[0] == TRILL_NET_ACK) {
    log_info("Probing done; we're the client");
    conn->tc_state = TC_STATE_CLIENT;

    // Start TLS handshaking.
    if (trill_start_tls(conn)) {
      trill_connected(conn, TRILL_ETLS);
    }
    return 0;
  }

  return 1;
}

/**
 * trill_send - Send a message to our peer over TLS.
 */
ssize_t
trill_send(struct trill_connection *conn, const void *data, size_t len)
{
  if (conn->tc_state != TC_STATE_ESTABLISHED) {
    errno = ENOTCONN;
    return -1;
  }

  return trill_tls_send(conn, data, len);
}


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

int
trill_get_fd(const struct trill_connection *conn)
{
  return conn->tc_fd;
}

uint16_t
trill_get_port(const struct trill_connection *conn)
{
  return conn->tc_port;
}

void
trill_set_data(struct trill_connection *conn, void *data)
{
  conn->tc_data = data;
}

void
trill_set_connect_cb(struct trill_connection *conn,
    trill_status_callback_t cb)
{
  conn->tc_connected_cb = cb;
}

void
trill_set_recv_callback(struct trill_connection *conn,
    trill_recv_callback_t cb)
{
  conn->tc_recv_cb = cb;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Motmot event layer wrappers.
//

int
trill_want_read(struct trill_connection *conn, motmot_event_callback_t cb)
{
  return motmot_event_want_read(conn->tc_fd, MOTMOT_EVENT_UDP,
      conn->tc_data, cb, (void *)conn);
}

int
trill_want_write(struct trill_connection *conn, motmot_event_callback_t cb)
{
  return motmot_event_want_write(conn->tc_fd, MOTMOT_EVENT_UDP,
      conn->tc_data, cb, (void *)conn);
}
