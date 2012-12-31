/**
 * request.h - Plume server request wrappers.
 */
#ifndef __TRILL_CLIENT_REQUEST_H__
#define __TRILL_CLIENT_REQUEST_H__

#include <stdint.h>

#include "common/msgpack_io.h"
#include "common/yakyak.h"

int req_route_identify(struct msgpack_conn *, char *);
int req_route_cert(struct msgpack_conn *, char *);
int req_route_connect(struct msgpack_conn *, char *, char *);
int req_reflection(struct msgpack_conn *, uint64_t);

#endif /* __TRILL_CLIENT_REQUEST_H__ */
