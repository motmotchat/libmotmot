/**
 * request.c - Plume server request wrappers.
 */

#include <stdint.h>
#include <string.h>

#include <msgpack.h>

#include "common/yakyak.h"
#include "plume/common.h"

void
msgpack_pack_string(msgpack_packer *pk, char *s)
{
  size_t len = strlen(s);
  msgpack_pack_raw(pk, len);
  msgpack_pack_raw_body(pk, s, len);
}


///////////////////////////////////////////////////////////////////////////////
//
//  Plume requests.
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
