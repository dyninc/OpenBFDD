/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Base header for the beacon and control
#pragma once
#include "config.h"
#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS
#include <sys/types.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>

#ifndef countof
#define countof(t) (sizeof(t)/sizeof((t)[0]))
#endif
