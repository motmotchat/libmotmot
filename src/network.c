#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "network.h"
#include "log.h"

trill_net_want_write_cb_t want_write_cb;
trill_net_want_timeout_cb_t want_timeout_cb;

int
trill_net_init(trill_net_want_write_cb_t want_write,
    trill_net_want_timeout_cb_t want_timeout)
{
  assert(want_write != NULL && "Want write callback is NULL");
  assert(want_timeout != NULL && "Want timeout callback is NULL");

  want_write_cb = want_write;
  want_timeout_cb = want_timeout;

  return 0;
}

struct trill_connection *
trill_connection_new()
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
  conn->tc_port = addr.sin_port;

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

  conn->tc_can_read_cb = trill_connection_can_read;
  conn->tc_can_write_cb = trill_connection_can_write;
  conn->tc_timeout_cb = trill_connection_broker;

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
  conn->tc_remote.sin_port = port;
  ret = inet_pton(AF_INET, remote, &conn->tc_remote.sin_addr);
  if (ret == 0) {
    log_warn("Unparseable remote address %s", remote);
    return -1;
  } else if (ret == -1) {
    log_errno("System error while parsing remote address");
    return -1;
  }

  want_timeout_cb(conn, 1000);

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
trill_connection_can_read(struct trill_connection *conn)
{
  // TODO: stub
  return 1;
}

int
trill_connection_can_write(struct trill_connection *conn)
{
  // TODO: stub
  return 1;
}

int
trill_connection_broker(struct trill_connection *conn)
{
  assert(conn->tc_remote.sin_port != 0 && "Connection does not have a remote");

  // TODO: stub
  log_info("About to broker a connection!");

  return 1;
}
