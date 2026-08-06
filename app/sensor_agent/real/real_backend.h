#ifndef REAL_BACKEND_H
#define REAL_BACKEND_H

#include "../../../common/include/presence_uapi.h"

int  real_backend_init(void);

int  real_backend_wait_read(struct presence_event *ev, int timeout_ms);

void real_backend_close(void);

#endif
