#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"
#include "crypto.h"
#include "log.h"
#include "network.h"

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

  // Let's go hunting for an IP address!
  if (getifaddrs(&interface_list)) {
    log_errno("Unable to list interfaces");
    if (trill_connection_free(conn)) {
      log_error("Error freeing connection");
    }
    return NULL;
  }
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

  trill_tls_init(conn);

  return conn;
}

int
trill_connect(struct trill_connection *conn, const char *remote,
    uint16_t port)
{
  int ret;
  struct sockaddr_in addr;

  assert(conn != NULL && "Attempting to connect with a null connection");
  assert(remote != NULL && "Connecting to a null address");

  bzero(&addr, sizeof(addr));
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

  trill_want_timeout_callback(conn->tc_event_loop_data, trill_connection_probe, 1000);

  return 0;
}

int
trill_connection_free(struct trill_connection *conn)
{
  int retval = 0;

  assert(conn != NULL && "Attempting to free a null connection");

  retval = trill_tls_free(conn);

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

  // Build the probe message. This looks like
  // +-----+---------------+--------------+
  // | ack | priority high | priority low |
  // +-----+---------------+--------------+
  // where ack is TRILL_NET_ACK if we've received a message from them, and
  // TRILL_NET_NOACK otherwise.
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

  // XXX: we make the assumption here that two 64-bit random numbers will
  // never collide. This is probably an okay assumption in practice, but is
  // sort of a hack
  a = ntohl(*(uint32_t *)(buf + 1));
  b = ntohl(*(uint32_t *)(buf + 5));
  winning = a > conn->tc_server_priority[0] ||
    (a == conn->tc_server_priority[0] && b > conn->tc_server_priority[0]);

  if (winning) {
    log_info("Probing done; we're the server");
    conn->tc_state = TC_STATE_SERVER;
    // Manually trigger the probe, since we have new data
    trill_connection_probe(conn);
    trill_start_tls(conn);
  } else if (!winning && buf[0] == TRILL_NET_ACK) {
    log_info("Probing done; we're the client");
    conn->tc_state = TC_STATE_CLIENT;
    trill_start_tls(conn);
  }

  return 1;
}
