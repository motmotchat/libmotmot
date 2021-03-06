/**
 * request.c - Plume server request wrappers.
 */

#include <stdint.h>
#include <string.h>

#include <msgpack.h>

#include "common/log.h"
#include "common/yakyak.h"
#include "plume/common.h"
#include "plume/plume.h"

#define BUFSIZE 4096

void
msgpack_pack_string(msgpack_packer *pk, char *s)
{
  size_t len = strlen(s);
  msgpack_pack_raw(pk, len);
  msgpack_pack_raw_body(pk, s, len);
}


///////////////////////////////////////////////////////////////////////////////
//
//  Response routines.
//

void plume_recv_identify(struct plume_client *, const void *, size_t,
    msgpack_object *);
void plume_recv_cert(struct plume_client *, const void *, size_t,
    msgpack_object *);
void plume_recv_connect(struct plume_client *, const void *, size_t,
    msgpack_object *);
void plume_recv_udp_ack(struct plume_client *, msgpack_object *);
void plume_recv_udp_echo(struct plume_client *, msgpack_object *);

/**
 * plume_recv_dispatch - Dispatch routine for Plume server messages.
 */
int
plume_recv_dispatch(struct plume_client *client, int status, void *data)
{
  ssize_t len;
  msgpack_unpacked result;
  msgpack_object *o, *p;
  int opcode;

  (void)status;
  (void)data;

  msgpack_unpacked_init(&result);

  // Reserve enough space in the msgpack buffer for a read.
  msgpack_unpacker_reserve_buffer(&client->pc_unpac, BUFSIZE);

  // Read up to BUFSIZE bytes into the stream.
  len = plume_recv(client, msgpack_unpacker_buffer(&client->pc_unpac), BUFSIZE);
  msgpack_unpacker_buffer_consumed(&client->pc_unpac, len);

  // Dispatch on as many msgpack objects as possible.
  while (msgpack_unpacker_next(&client->pc_unpac, &result)) {
    o = &result.data;

    // Discard malformed responses.
    if (o->type != MSGPACK_OBJECT_ARRAY ||
        o->via.array.size < 2 || o->via.array.size > 3) {
      log_error("Discarding malformed Plume server payload");
      continue;
    }
    p = o->via.array.ptr;

    // Pull out the opcode.
    if (p->type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
      log_error("Discarding malformed Plume server payload");
      continue;
    }
    opcode = (p++)->via.u64;

    // Dispatch.
    switch (opcode) {
      case PLUME_OP_IDENTIFY:
        plume_recv_identify(client, p->via.raw.ptr, p->via.raw.size, p + 1);
        break;
      case PLUME_OP_CERT:
        plume_recv_cert(client, p->via.raw.ptr, p->via.raw.size, p + 1);
        break;
      case PLUME_OP_CONNECT:
        plume_recv_connect(client, p->via.raw.ptr, p->via.raw.size, p + 1);
        break;
      case PLUME_OP_UDP_ACK:
        plume_recv_udp_ack(client, p);
      case PLUME_OP_UDP_ECHO:
        plume_recv_udp_echo(client, p);
        break;
      default:
        log_error("Discarding malformed Plume server payload");
        break;
    }
  }

  msgpack_unpacked_destroy(&result);
  return 0;
}

/**
 * plume_recv_identify - Receive a peer's request for our cert.
 */
void
plume_recv_identify(struct plume_client *client, const void *peer_cert,
    size_t peer_cert_size, msgpack_object *o)
{
}

/**
 * plume_recv_cert - Receive a cert from a peer.
 */
void
plume_recv_cert(struct plume_client *client, const void *peer_cert,
    size_t peer_cert_size, msgpack_object *o)
{
}

/**
 * plume_recv_connect - Receive a peer's connection information.
 */
void
plume_recv_connect(struct plume_client *client, const void *peer_cert,
    size_t peer_cert_size, msgpack_object *o)
{
}

/**
 * plume_recv_udp_ack - Receive the server's acknowledgment of our UDP
 * reflection request.
 */
void
plume_recv_udp_ack(struct plume_client *client, msgpack_object *o)
{
}

/**
 * plume_recv_udp_echo - Receive the server's reflection of our UDP socket's
 * net address.
 */
void
plume_recv_udp_echo(struct plume_client *client, msgpack_object *o)
{
}


///////////////////////////////////////////////////////////////////////////////
//
//  Plume server requests.
//

/**
 * plume_req_init - Initialize a yakyak, then pack an opcode and our cert.
 */
void
plume_req_init(struct yakyak *yy, struct plume_client *client, char *op,
    unsigned n)
{
  yakyak_init(yy, 2);

  msgpack_pack_string(yy->pk, op);
  msgpack_pack_array(yy->pk, n + 1);
  msgpack_pack_raw(yy->pk, client->pc_cert_size);
  msgpack_pack_raw_body(yy->pk, client->pc_cert, client->pc_cert_size);
}

/**
 * plume_req_init_route - Initialize a route request, additionally packing our
 * peer's handle and an opcode for our peer.
 */
void
plume_req_init_route(struct yakyak *yy, struct plume_client *client, char *op,
    char *peer_handle, unsigned n)
{
  plume_req_init(yy, client, "route", n + 2);

  msgpack_pack_string(yy->pk, peer_handle);
  msgpack_pack_string(yy->pk, op);
}

/**
 * plume_req_route_identify - Request a peer's cert.
 */
int
plume_req_route_identify(struct plume_client *client, char *peer_handle)
{
  struct yakyak yy;

  plume_req_init_route(&yy, client, "identify", peer_handle, 0);

  return plume_send(client, yakyak_data(&yy), yakyak_size(&yy));
}

/**
 * plume_req_route_cert - Send a peer our cert.
 */
int
plume_req_route_cert(struct plume_client *client, char *peer_handle)
{
  struct yakyak yy;

  plume_req_init_route(&yy, client, "cert", peer_handle, 0);

  return plume_send(client, yakyak_data(&yy), yakyak_size(&yy));
}

/**
 * plume_req_route_connect - Send a peer our calling card.
 */
int
plume_req_route_connect(struct plume_client *client, char *peer_handle)
{
  struct yakyak yy;

  plume_req_init_route(&yy, client, "connect", peer_handle, 1);
  msgpack_pack_string(yy.pk, "callingcard");

  return plume_send(client, yakyak_data(&yy), yakyak_size(&yy));
}

/**
 * plume_req_reflection - Request UDP reflection service from the Plume server.
 */
int
plume_req_reflection(struct plume_client *client, uint64_t *cookie)
{
  struct yakyak yy;

  plume_req_init(&yy, client, "reflect", 1);
  msgpack_pack_uint64(yy.pk, *cookie);

  return plume_send(client, yakyak_data(&yy), yakyak_size(&yy));
}
