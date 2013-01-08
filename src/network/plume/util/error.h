/**
 * error.h - Plume error utilities.
 */
#ifndef __PLUME_ERROR_H__
#define __PLUME_ERROR_H__

#include "plume/plume.h"

const char *plume_strerror(enum plume_status);
enum plume_status error_ares(int);

#endif /* __PLUME_ERROR_H__ */
