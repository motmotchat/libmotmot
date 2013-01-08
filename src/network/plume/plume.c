/**
 * plume.c - Plume client interface.
 */
#include <assert.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ares.h>

#include "common/log.h"
#include "common/readfile.h"

#include "event/callbacks.h"
#include "plume/plume.h"
#include "plume/common.h"
#include "plume/tls.h"
#include "plume/util/email.h"
#include "plume/util/error.h"

#define PLUME_SRV_PREFIX  "_plume._tcp."

static int  plume_ares_channel_init(ares_channel *);
static void plume_ares_want_io(void *, int, int, int);

/**
 * plume_init - Initialize the Plume client service.
 */
int
plume_init()
{
  int r;

  if (!motmot_event_did_init()) {
    log_error("Failed to init Motmot event layer");
    return -1;
  }

  if ((r = plume_crypto_init())) {
    return r;
  }

  if ((r = ares_library_init(ARES_LIB_INIT_ALL))) {
    log_error("Error initializing c-ares");
    return r;
  }

  return 0;
}

/**
 * plume_client_new - Instantiate a new Plume client object.
 */
struct plume_client *
plume_client_new(const char *cert_path)
{
  struct plume_client *client;
  int flags;

  assert(cert_path != NULL && "No identity cert specified for new client.");

  client = calloc(1, sizeof(*client));
  if (client == NULL) {
    return NULL;
  }

  // Read in the client's identity cert and extract the CN.
  client->pc_cert = readfile(cert_path, &client->pc_cert_size);
  if (client->pc_cert == NULL) {
    goto err;
  }
  client->pc_handle = plume_crt_get_cn(client->pc_cert, client->pc_cert_size);
  if (client->pc_handle == NULL) {
    goto err;
  }

  // Initialize the TLS backend.
  if (plume_tls_init(client)) {
    goto err;
  }

  // Initialize the DNS query channels (c-ares).
  if (plume_ares_channel_init(&client->pc_ares_chan_srv) ||
      plume_ares_channel_init(&client->pc_ares_chan_host)) {
    goto err;
  }

  // Create a new nonblocking TCP socket.
  client->pc_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client->pc_fd == -1) {
    goto err;
  }
  flags = fcntl(client->pc_fd, F_GETFL, 0);
  if (flags == -1 || fcntl(client->pc_fd, F_SETFL, flags | O_NONBLOCK)) {
    goto err;
  }

  return client;

err:
  plume_client_destroy(client);
  return NULL;
}

/**
 * plume_client_destroy - Destroy a Plume client object, closing the server
 * connection if it is still live.
 */
int
plume_client_destroy(struct plume_client *client)
{
  int retval = 0;

  assert(client != NULL && "Attempting to free a null client");

  retval = plume_tls_deinit(client);

  if (client->pc_fd != -1 && close(client->pc_fd) == -1) {
    log_errno("Error closing Plume server connection");
    retval = -1;
  }

  ares_cancel(client->pc_ares_chan_srv);
  ares_destroy(client->pc_ares_chan_srv);

  ares_cancel(client->pc_ares_chan_host);
  ares_destroy(client->pc_ares_chan_host);

  free(client->pc_handle);
  free(client->pc_cert);
  free(client->pc_host);
  free(client->pc_ip);
  free(client);

  return retval;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Connect protocol.
//
//  The Plume server connection protocol is a multi-step asynchronous process,
//  which entails the following:
//
//  1.  Send a DNS query for the Plume server's _plume._tcp SRV record.
//  2.  Receive the SRV record and send a query for the server's IP.
//  3.  Open a TCP socket to the server, having obtained its IP.
//  4.  Begin TLS handshaking.
//

static void plume_dns_lookup(void *, int, int, unsigned char *, int);
static void plume_socket_connect(void *, int, int, struct hostent *);
static int  plume_tls_begin(void *);

/**
 * plume_connect_server - Begin connecting to the client's Plume server by
 * looking up the Plume SRV record.
 */
void
plume_connect_server(struct plume_client *client)
{
  char *domain, *srvname;
  unsigned char *qbuf;
  int status, qbuflen;

  assert(client != NULL && "Attempting to connect with a null client");

  if (client->pc_started) {
    return client->pc_connect(client, PLUME_EINUSE, client->pc_data);
  }
  client->pc_started = 1;

  // Pull the domain from the client's handle.
  domain = email_get_domain(client->pc_handle);
  if (domain == NULL) {
    return client->pc_connect(client, PLUME_EIDENTITY, client->pc_data);
  }

  // Get the Plume server SRV record name.
  srvname = malloc(strlen(PLUME_SRV_PREFIX) + strlen(domain) + 1);
  if (srvname == NULL) {
    return client->pc_connect(client, PLUME_ENOMEM, client->pc_data);
  }
  strcpy(srvname, PLUME_SRV_PREFIX);
  strcat(srvname, domain);

  // Fill a DNS query buffer.
  status = ares_mkquery(srvname, ns_c_in, ns_t_srv, 0, 1, &qbuf, &qbuflen);
  if (status != ARES_SUCCESS) {
    return client->pc_connect(client, error_ares(status), client->pc_data);
  }
  free(srvname);

  // Start a DNS lookup.
  ares_send(client->pc_ares_chan_srv, qbuf, qbuflen, plume_dns_lookup, client);
  ares_free_string(qbuf);
}

/**
 * plume_dns_lookup - Resolve the SRV lookup and perform a hostname lookup.
 */
static void plume_dns_lookup(void *data, int status, int timeouts,
    unsigned char *abuf, int alen)
{
  struct plume_client *client;
  struct ares_srv_reply *srv;

  client = (struct plume_client *)data;

  if (status != ARES_SUCCESS) {
    return client->pc_connect(client, error_ares(status), client->pc_data);
  }
  if (abuf == NULL) {
    return client->pc_connect(client, PLUME_EDNS, client->pc_data);
  }

  // Parse the SRV query payload.
  status = ares_parse_srv_reply(abuf, alen, &srv);
  if (status != ARES_SUCCESS) {
    return client->pc_connect(client, error_ares(status), client->pc_data);
  }

  // Store the retrieved hostname and port.
  client->pc_host = strdup(srv->host);
  if (client->pc_host == NULL) {
    return client->pc_connect(client, PLUME_ENOMEM, client->pc_data);
  }
  client->pc_port = srv->port;
  log_info("SRV:  %s:%d", client->pc_host, client->pc_port);

  ares_free_data(srv);

  // Send off a hostaddr lookup.
  ares_gethostbyname(client->pc_ares_chan_host, client->pc_host, AF_INET,
      plume_socket_connect, client);
}

/**
 * plume_socket_connect - Resolve the hostaddr query and connect.
 */
static void plume_socket_connect(void *data, int status, int timeouts,
    struct hostent *host)
{
  struct plume_client *client;
  struct sockaddr_in addr;

  client = (struct plume_client *)data;

  if (status != ARES_SUCCESS) {
    return client->pc_connect(client, error_ares(status), client->pc_data);
  }
  if (host == NULL) {
    return client->pc_connect(client, PLUME_EDNS, client->pc_data);
  }

  // Store the retrieved IP address.
  client->pc_ip = malloc(INET_ADDRSTRLEN);
  if (client->pc_ip == NULL) {
    return client->pc_connect(client, PLUME_ENOMEM, client->pc_data);
  }
  inet_ntop(host->h_addrtype, host->h_addr, client->pc_ip, INET_ADDRSTRLEN);
  log_info("host: %s:%d", client->pc_ip, client->pc_port);

  addr.sin_family = host->h_addrtype;
  addr.sin_port = htons(client->pc_port);
  memcpy(&addr.sin_addr, host->h_addr, host->h_length);

  // Perform a nonblocking connect.  We then listen for the socket to be
  // writeable to confirm success or failure.
  if (connect(client->pc_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    if (errno == EINPROGRESS) {
      plume_want_write(client, plume_tls_begin);
    } else {
      return client->pc_connect(client, -errno, client->pc_data);
    }
  }
}

/**
 * plume_tls_begin - Confirm the success of connect() and begin the TLS
 * handshake.
 */
static int
plume_tls_begin(void *data)
{
  int r;
  socklen_t len = sizeof(r);
  struct plume_client *client;

  client = (struct plume_client *)data;

  // Check for connect errors.
  if (getsockopt(client->pc_fd, SOL_SOCKET, SO_ERROR, &r, &len)) {
    client->pc_connect(client, -errno, client->pc_data);
    return 0;
  }
  if (r) {
    client->pc_connect(client, -r, client->pc_data);
    return 0;
  }

  // TODO: TLS handshaking.

  log_info("Connection completed");

  return 0;
}


///////////////////////////////////////////////////////////////////////////////
//
//  DNS-event interface helpers.
//

struct plume_ares_sock {
  int fd;                 // the socket's fd
  int is_read;            // i/o event we care about
  ares_channel *channel;  // the associated lookup channel
};

/**
 * plume_ares_channel_init - Initialize a ares_channel, hooking it into the
 * Motmot event layer.
 */
static int
plume_ares_channel_init(ares_channel *channel)
{
  struct ares_options options;
  int status, optmask;

  options.sock_state_cb = plume_ares_want_io;
  options.sock_state_cb_data = channel;
  optmask = ARES_OPT_SOCK_STATE_CB;

  status = ares_init_options(channel, &options, optmask);

  return status != ARES_SUCCESS;
}


/**
 * plume_ares_sock_new - Make a new fd/client wrapper.
 */
static struct plume_ares_sock *
plume_ares_sock_new(int fd, int is_read, ares_channel *channel)
{
  struct plume_ares_sock *s;

  s = malloc(sizeof(*s));
  assert(s != NULL);

  s->fd = fd;
  s->is_read = is_read;
  s->channel = channel;

  return s;
}

/**
 * plume_ares_process - Process a single ares_channel socket after a read/write
 * event on it is triggered.
 */
static int
plume_ares_process(void *data)
{
  struct plume_ares_sock *s;

  s = (struct plume_ares_sock *)data;

  ares_process_fd(*s->channel,
      s->is_read ? s->fd : ARES_SOCKET_BAD,
      s->is_read ? ARES_SOCKET_BAD : s->fd);

  free(s);

  // Stop listening.
  return 0;
}

/**
 * plume_ares_want_io - Request socket event notification from the event loop
 * for a DNS query.
 *
 * There's no clean way to notify the connect protocol or the user that we were
 * unable to listen on the ares_channel sockets.  Instead, we just rely on the
 * main connect timeout to kill the plume_client.
 *
 * TODO: Implement the main connect timeout.  We'll need to somehow clean up
 * these event listeners along with the client object.
 */
static void
plume_ares_want_io(void *data, int fd, int read, int write)
{
  struct plume_ares_sock *s;

  if (read) {
    s = plume_ares_sock_new(fd, 1, (ares_channel *)data);
    motmot_event_want_read(fd, MOTMOT_EVENT_UDP, NULL, plume_ares_process, s);
  }
  if (write) {
    s = plume_ares_sock_new(fd, 0, (ares_channel *)data);
    motmot_event_want_write(fd, MOTMOT_EVENT_UDP, NULL, plume_ares_process, s);
  }
}


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

int
plume_client_get_fd(const struct plume_client *client)
{
  return client->pc_fd;
}

void
plume_client_set_data(struct plume_client *client, void *data)
{
  client->pc_data = data;
}

void
plume_client_set_connect_cb(struct plume_client *client,
    plume_connect_callback_t cb)
{
  client->pc_connect = cb;
}

void
plume_client_set_recv_cb(struct plume_client *client,
    plume_recv_callback_t cb)
{
  client->pc_recv = cb;
}


///////////////////////////////////////////////////////////////////////////////
//
//  Motmot event layer wrappers.
//

int
plume_want_read(struct plume_client *client, motmot_event_callback_t cb)
{
  return motmot_event_want_read(client->pc_fd, MOTMOT_EVENT_TCP,
      client->pc_data, cb, (void *)client);
}

int
plume_want_write(struct plume_client *client, motmot_event_callback_t cb)
{
  return motmot_event_want_write(client->pc_fd, MOTMOT_EVENT_TCP,
      client->pc_data, cb, (void *)client);
}
