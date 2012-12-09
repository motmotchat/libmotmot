#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "network.h"
#include "log.h"

struct trill_connection *
trill_connection_new()
{
  struct trill_connection *conn;
  struct sockaddr_in addr;
  socklen_t addr_len;
  int ret;

  conn = malloc(sizeof(*conn));
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
