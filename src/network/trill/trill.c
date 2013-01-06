/**
 * trill.c - Trill public interface.
 */
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "trill/common.h"
#include "trill/crypto.h"
#include "trill/network.h"

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
  return trill_crypto_init();
}

/**
 * TRILL_COND_LRF - Conditionally log, free a connection, and return NULL.
 */
#define TRILL_COND_LFR(cond, conn, msg)  \
  if (cond) {                           \
    log_errno(msg);                     \
    trill_connection_free(conn);        \
    return NULL;                        \
  }

/**
 * trill_connection_new - Instantiate a new Trill connection object.
 */
struct trill_connection *
trill_connection_new()
{
  struct trill_connection *conn;
  struct sockaddr_in addr;
  socklen_t addr_len;
  struct ifaddrs *interface_list, *interface;
  int ret, flags;

  conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    return NULL;
  }

  conn->tc_sock_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  TRILL_COND_LFR(conn->tc_sock_fd == -1, conn, "Unable to create socket");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 0;  // Pick a random port.

  // Let's go hunting for an IP address!
  TRILL_COND_LFR(getifaddrs(&interface_list), conn, "Unable to list ifaddrs");

  for (interface = interface_list; interface; interface = interface->ifa_next) {
    if ((interface->ifa_flags & IFF_UP) &&
        !(interface->ifa_flags & IFF_LOOPBACK) &&
        interface->ifa_addr->sa_family == AF_INET) {
      addr.sin_addr.s_addr =
        ((struct sockaddr_in *)interface->ifa_addr)->sin_addr.s_addr;
    }
  }
  freeifaddrs(interface_list);

  ret = bind(conn->tc_sock_fd, (struct sockaddr *) &addr, sizeof(addr));
  TRILL_COND_LFR(ret == -1, conn, "Unable to bind socket");

  addr_len = sizeof(addr);
  ret = getsockname(conn->tc_sock_fd, (struct sockaddr *) &addr, &addr_len);
  TRILL_COND_LFR(ret == -1, conn, "Unable to get socket name");
  conn->tc_port = ntohs(addr.sin_port);

  // DTLS requires that we disable IP fragmentation.
  //
  // It appears that OS X doesn't support this without writing raw UDP packets
  // ourselves, and we can't do that.  "Oh well."
  //
  // http://lists.apple.com/archives/macnetworkprog/2006/Jul/msg00018.html
#if defined IP_DONTFRAG
  int optval = 1;
  setsockopt(conn->tc_sock_fd, IPROTO_IP, IP_DONTFRAG, &optval, sizeof(optval));
#elif defined IP_MTU_DISCOVER
  int optval = IP_PMTUDISC_DO;
  setsockopt(conn->tc_sock_fd, IPROTO_IP, IP_MTU_DISCOVER, &optval,
      sizeof(optval));
#endif

  flags = fcntl(conn->tc_sock_fd, F_GETFL, 0);
  if (flags == -1 || fcntl(conn->tc_sock_fd, F_SETFL, flags | O_NONBLOCK)) {
    log_error("Error setting socket in nonblocking mode");
    trill_connection_free(conn);
    return NULL;
  }

  if (trill_tls_init(conn)) {
    log_error("Error initializing TLS backend data");
    trill_connection_free(conn);
    return NULL;
  }

  conn->tc_state = TC_STATE_INIT;

  conn->tc_server_priority[0] = random();
  conn->tc_server_priority[1] = random();

  conn->tc_can_read_cb = trill_connection_read_probe;
  conn->tc_can_write_cb = NULL;

  return conn;
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

  if (conn->tc_sock_fd != -1) {
    if (close(conn->tc_sock_fd) == -1) {
      log_errno("Error closing Trill connection socket");
      retval = -1;
    }
  }

  free(conn);

  return retval;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Connect protocol.
//

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

  connect(conn->tc_sock_fd, (struct sockaddr *) &addr, sizeof(addr));

  conn->tc_state = TC_STATE_PROBING;
  conn->tc_remote_user = strdup(who);

  trill_want_read(conn);
  motmot_event_want_timeout(trill_connection_probe, conn, conn->tc_data, 1000);

  return 0;
}

/**
 * trill_connection_probe - Send a probe message to our peer.
 */
int
trill_connection_probe(void *arg)
{
  struct trill_connection *conn;
  char buf[9];

  conn = (struct trill_connection *)arg;

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
  } else if (conn->tc_state == TC_STATE_ESTABLISHED) {
    return 0;
  } else {
    assert(0 && "Probing while in a bad state");
  }
  *(uint32_t *)(buf + 1) = htonl(conn->tc_server_priority[0]);
  *(uint32_t *)(buf + 5) = htonl(conn->tc_server_priority[1]);

  if (send(conn->tc_sock_fd, buf, sizeof(buf), 0) < 0) {
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
trill_connection_read_probe(struct trill_connection *conn)
{
  char buf[10];
  uint32_t a, b;
  int len, winning;

  assert((conn->tc_state == TC_STATE_PROBING ||
      conn->tc_state == TC_STATE_CLIENT) &&
      "Bad state when reading probe");

  len = recv(conn->tc_sock_fd, buf, sizeof(buf), 0);
  if (len > 0) {
    log_info("Received a message");
  } else {
    log_errno("Error reading a probe");
  }

  if (len != 9) {
    log_warn("Probe was a bad length; discarding");
    return 1;
  }

  // XXX: We make the assumption here that two 64-bit random numbers will
  // never collide.  This is probably an okay assumption in practice, but is
  // sort of a hack.
  a = ntohl(*(uint32_t *)(buf + 1));
  b = ntohl(*(uint32_t *)(buf + 5));
  winning = a > conn->tc_server_priority[0] ||
    (a == conn->tc_server_priority[0] && b > conn->tc_server_priority[0]);

  if (winning) {
    log_info("Probing done; we're the server");
    conn->tc_state = TC_STATE_SERVER;
    // Manually trigger the probe, since we have new data.
    trill_connection_probe(conn);
    trill_start_tls(conn);
  } else if (!winning && buf[0] == TRILL_NET_ACK) {
    log_info("Probing done; we're the client");
    conn->tc_state = TC_STATE_CLIENT;
    trill_start_tls(conn);
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
    errno = ENOTCONN; // XXX: is it okay to spoof errno like this?
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
  return conn->tc_sock_fd;
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
trill_set_connected_callback(struct trill_connection *conn,
    trill_connected_callback_t callback)
{
  conn->tc_connected_cb = callback;
}

void
trill_set_recv_callback(struct trill_connection *conn,
    trill_recv_callback_t callback)
{
  conn->tc_recv_cb = callback;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Motmot event layer wrappers.
//

int
trill_want_read(struct trill_connection *conn)
{
  return motmot_event_want_read(conn->tc_sock_fd, MOTMOT_EVENT_UDP,
      conn->tc_data, trill_can_read, (void *)conn);
}

int
trill_want_write(struct trill_connection *conn)
{
  return motmot_event_want_write(conn->tc_sock_fd, MOTMOT_EVENT_UDP,
      conn->tc_data, trill_can_write, (void *)conn);
}

int
trill_can_read(void *data)
{
  struct trill_connection *conn = (struct trill_connection *)data;
  return conn->tc_can_read_cb(conn);
}

int
trill_can_write(void *data)
{
  struct trill_connection *conn = (struct trill_connection *)data;
  return conn->tc_can_write_cb(conn);
}
