/**
 * request.h - Plume server request wrappers.
 */
#ifndef __PLUME_REQUEST_H__
#define __PLUME_REQUEST_H__

#include <stdint.h>

int plume_req_route_identify(struct plume_client *, char *);
int plume_req_route_cert(struct plume_client *, char *);
int plume_req_route_connect(struct plume_client *, char *);
int plume_req_reflection(struct plume_client *, uint64_t *);

#endif /* __PLUME_REQUEST_H__ */
