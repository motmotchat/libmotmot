#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "network.h"
#include "log.h"

int
trill_net_init(trill_net_want_write_cb_t want_write,
    trill_net_want_timeout_cb_t want_timeout)
{
  assert(want_write != NULL && "Want write callback is NULL");
  assert(want_timeout != NULL && "Want timeout callback is NULL");

  trill_net_want_write_cb = want_write;
  trill_net_want_timeout_cb = want_timeout;

  return 0;
}

struct trill_connection *
trill_connection_new(struct trill_crypto_identity *id)
{
  struct trill_connection *conn;
  struct sockaddr_in addr;
  socklen_t addr_len;
  int ret, flags;

  conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    return NULL;
  }

  conn->tc_sock_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (conn->tc_sock_fd == -1) {
    log_errno("Unable to create socket");
    if (trill_connection_free(conn)) {
      log_errno("Error freeing connection");
    }
    return NULL;
  }

  bzero(&addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 0; // Pick a random port
  addr.sin_addr.s_addr = INADDR_ANY;

  ret = bind(conn->tc_sock_fd, (struct sockaddr *) &addr, sizeof(addr));
  if (ret == -1) {
    log_errno("Unable to bind socket");
    if (trill_connection_free(conn)) {
      log_error("Error freeing connection");
    }
    return NULL;
  }

  addr_len = sizeof(addr);
  ret = getsockname(conn->tc_sock_fd, (struct sockaddr *) &addr, &addr_len);
  if (ret == -1) {
    log_errno("Unable to get socket name");
    if (trill_connection_free(conn)) {
      log_error("Error freeing connection");
    }
    return NULL;
  }
  conn->tc_port = ntohs(addr.sin_port);

  // DTLS requires that we disable IP fragmentation
  // It appears that OS X doesn't support this without writing raw UDP packets
  // ourselves, and we can't do that. "Oh well."
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
    if (trill_connection_free(conn)) {
      log_error("Error freeing connection");
    }
    return NULL;
  }

  conn->tc_state = TC_STATE_INIT;

  conn->tc_server_priority[0] = random();
  conn->tc_server_priority[1] = random();

  conn->tc_can_read_cb = trill_connection_read_probe;
  conn->tc_can_write_cb = NULL;
  conn->tc_timeout_cb = trill_connection_probe;

  conn->tc_id = id;

  return conn;
}

int
trill_connection_connect(struct trill_connection *conn, const char *remote,
    uint16_t port)
{
  int ret;

  assert(conn != NULL && "Attempting to connect with a null connection");
  assert(remote != NULL && "Connecting to a null address");

  bzero(&conn->tc_remote, sizeof(conn->tc_remote));
  conn->tc_remote.sin_family = AF_INET;
  conn->tc_remote.sin_port = htons(port);
  ret = inet_pton(AF_INET, remote, &conn->tc_remote.sin_addr);
  if (ret == 0) {
    log_warn("Unparseable remote address %s", remote);
    return -1;
  } else if (ret == -1) {
    log_errno("System error while parsing remote address");
    return -1;
  }

  conn->tc_state = TC_STATE_PROBING;

  trill_net_want_timeout_cb(conn, 1000);

  return 0;
}

int
trill_connection_free(struct trill_connection *conn)
{
  int retval = 0;
  assert(conn != NULL && "Attempting to free a null connection");
  if (conn->tc_sock_fd != -1) {
    if (close(conn->tc_sock_fd) == -1) {
      log_errno("Error closing socket");
      retval = 1;
    }
  }

  free(conn);

  return retval;
}

int
trill_connection_probe(struct trill_connection *conn)
{
  char buf[9];

  if (conn->tc_state == TC_STATE_HANDSHAKE_CLIENT) {
    return 0;
  }

  assert(conn->tc_state == TC_STATE_PROBING ||
      conn->tc_state == TC_STATE_PRESHAKE_CLIENT);

  // Build the probe message. This looks like
  // +-----+---------------+--------------+
  // | c'd | priority high | priority low |
  // +-----+---------------+--------------+
  // where connected is a single 1 byte if we've received a message from the
  // other party, and 0 otherwise.
  // We manually order the 32-bit priorities since there doesn't appear to be a
  // cross-platform 64-bit htonl-like function.
  buf[0] = conn->tc_state == TC_STATE_PRESHAKE_SERVER ||
    conn->tc_state == TC_STATE_PRESHAKE_CLIENT;
  *(uint32_t *)(buf + 1) = htonl(conn->tc_server_priority[0]);
  *(uint32_t *)(buf + 5) = htonl(conn->tc_server_priority[1]);

  if (sendto(conn->tc_sock_fd, buf, sizeof(buf), 0,
      (struct sockaddr *)&conn->tc_remote, sizeof(conn->tc_remote)) < 0) {
    if (errno != EAGAIN && errno != EINTR) {
      log_errno("Error sending a probe");
    }
  }

  return 1;
}

int
trill_connection_read_probe(struct trill_connection *conn)
{
  char buf[9];
  uint32_t a, b;
  char raddr[32];
  struct sockaddr_in remote;
  int len;
  socklen_t retlen = sizeof(remote);

  len = recvfrom(conn->tc_sock_fd, buf, sizeof(buf), 0,
      (struct sockaddr *)&remote, &retlen);
  if (len > 0) {
    inet_ntop(AF_INET, &remote, raddr, sizeof(raddr));
    log_info("Received a message from %s:%d", raddr, ntohs(remote.sin_port));
  } else {
    log_errno("Error reading a probe");
  }

  if (len == 1 && buf[0] == '\x02') {
    conn->tc_state = TC_STATE_HANDSHAKE_CLIENT;
    trill_crypto_session_init(conn);
    return 1;
  } else if (len != 9) {
    log_error("Probe was a bad length; discarding");
    return 1;
  }

  a = ntohl(*(uint32_t *)(buf + 1));
  b = ntohl(*(uint32_t *)(buf + 5));
  if (conn->tc_state == TC_STATE_PROBING) {
    // XXX: we make the assumption here that two 64-bit random numbers will
    // never collide. This is probably an okay assumption in practice, but is
    // sort of a hack
    if (a > conn->tc_server_priority[0] ||
        (a == conn->tc_server_priority[0] && b > conn->tc_server_priority[0])) {
      // I'm the server!
      conn->tc_timeout_cb = trill_connection_go_ahead;
      log_warn("I'm the server!");
      if (buf[0]) {
        conn->tc_state = TC_STATE_HANDSHAKE_SERVER;
        trill_crypto_session_init(conn);
      } else {
        conn->tc_state = TC_STATE_PRESHAKE_SERVER;
      }
    } else {
      if (buf[0]) conn->tc_state = TC_STATE_HANDSHAKE_CLIENT;
      else        conn->tc_state = TC_STATE_PRESHAKE_CLIENT;
    }
  }

  return 1;
}

int
trill_connection_go_ahead(struct trill_connection *conn)
{
  if (conn->tc_state == TC_STATE_ENCRYPTED) {
    return 0;
  }

  log_warn("Go ahead!");

  // Send the single byte "2"
  if (sendto(conn->tc_sock_fd, "\x02", 1, 0,
        (struct sockaddr *)&conn->tc_remote, sizeof(conn->tc_remote)) < 0) {
    if (errno != EAGAIN && errno != EINTR) {
      log_errno("Error sending a probe");
    }
  }

  return 1;
}
