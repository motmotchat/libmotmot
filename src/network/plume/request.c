/**
 * request.c - Plume server request wrappers.
 */

#include <stdint.h>
#include <string.h>

#include <msgpack.h>

#include "common/msgpack_io.h"
#include "common/yakyak.h"

#include "network/plume/x509.h"

void
msgpack_pack_string(msgpack_packer *pk, char *s)
{
  size_t len = strlen(s);
  msgpack_pack_raw(pk, len);
  msgpack_pack_raw_body(pk, s, len);
}

void
req_init(struct yakyak *yy, char *op, unsigned n)
{
  yakyak_init(yy, 2);

  msgpack_pack_string(yy->pk, op);
  msgpack_pack_array(yy->pk, n + 1);
  msgpack_pack_string(yy->pk, "cert");
}

void
req_init_route(struct yakyak *yy, char *op, char *peer_handle, unsigned n)
{
  req_init(yy, "route", n + 2);

  msgpack_pack_string(yy->pk, peer_handle);
  msgpack_pack_string(yy->pk, op);
}

int
req_route_identify(struct msgpack_conn *conn, char *peer_handle)
{
  struct yakyak yy;

  req_init_route(&yy, "identify", peer_handle, 0);

  return msgpack_conn_send(conn, yakyak_data(&yy), yakyak_size(&yy));
}

int
req_route_cert(struct msgpack_conn *conn, char *peer_handle)
{
  struct yakyak yy;

  req_init_route(&yy, "cert", peer_handle, 0);

  return msgpack_conn_send(conn, yakyak_data(&yy), yakyak_size(&yy));
}

int
req_route_connect(struct msgpack_conn *conn, char *peer_handle, char *id_enc)
{
  struct yakyak yy;

  req_init_route(&yy, "connect", peer_handle, 1);
  msgpack_pack_string(yy.pk, id_enc);

  return msgpack_conn_send(conn, yakyak_data(&yy), yakyak_size(&yy));
}

int
req_reflection(struct msgpack_conn *conn, uint64_t cookie)
{
  struct yakyak yy;

  req_init(&yy, "reflect", 1);
  msgpack_pack_uint64(yy.pk, cookie);

  return msgpack_conn_send(conn, yakyak_data(&yy), yakyak_size(&yy));
}
