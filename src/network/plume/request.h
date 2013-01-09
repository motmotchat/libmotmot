/**
 * request.h - Plume server request wrappers.
 */
#ifndef __PLUME_REQUEST_H__
#define __PLUME_REQUEST_H__

#include <stdint.h>

#include "common/msgpack_io.h"
#include "common/yakyak.h"

int plume_req_route_identify(struct msgpack_conn *, char *);
int plume_req_route_cert(struct msgpack_conn *, char *);
int plume_req_route_connect(struct msgpack_conn *, char *);
int plume_req_reflection(struct msgpack_conn *, uint64_t *);

#endif /* __PLUME_REQUEST_H__ */
