/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* TILT's network side, built against the SDK's lwIP headers: the remote (remote.c), nothing else */
#include <stdint.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "shim.h"
#include "plat.h"

extern int snprintf(char *buf, unsigned long sz, const char *f, ...);
extern void os_time_dly(int ticks);

#include "remote.c"
