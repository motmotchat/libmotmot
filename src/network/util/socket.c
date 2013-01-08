/**
 * socket.c - Motmot socket utilities.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include "common/log.h"
#include "util/socket.h"

/**
 * socket_udp_nonblock - Open up a nonblocking UDP socket on a random port.
 * The socket is returned and the port is passed via the reference argument.
 */
int
socket_udp_nonblock(uint16_t *port)
{
  int s, flags;
  struct sockaddr_in addr;
  socklen_t addr_len;
  struct ifaddrs *interface_list, *ifa;

  assert(port != NULL);

  // Create a new socket.
  s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == -1) {
    log_errno("Unable to create socket");
    return -1;
  }

  // Make it nonblocking.
  flags = fcntl(s, F_GETFL, 0);
  if (flags == -1 || fcntl(s, F_SETFL, flags | O_NONBLOCK)) {
    log_error("Error setting socket in nonblocking mode");
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 0;  // Pick a random port.

  // Determine our IP address.
  if (getifaddrs(&interface_list)) {
    log_errno("Unable to list ifaddrs");
    return -1;
  }
  for (ifa = interface_list; ifa; ifa = ifa->ifa_next) {
    if ((ifa->ifa_flags & IFF_UP) && !(ifa->ifa_flags & IFF_LOOPBACK) &&
        ifa->ifa_addr->sa_family == AF_INET) {
      addr.sin_addr.s_addr =
        ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
    }
  }
  freeifaddrs(interface_list);

  // Bind the socket to our IP.
  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    log_errno("Unable to bind socket");
    return -1;
  }

  // Determine the port.
  addr_len = sizeof(addr);
  if (getsockname(s, (struct sockaddr *)&addr, &addr_len) == -1) {
    log_errno("Unable to get socket name");
    return -1;
  }

  *port = ntohs(addr.sin_port);
  return s;
}
