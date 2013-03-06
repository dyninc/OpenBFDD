/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "standard.h"
#include <string.h>
#include <cstdio>

using namespace std;


#ifndef HAS_STRERROR_R
#error no strerror_r() function found.
#endif

#ifdef IS_ISO_STRERROR_R
void compat_strerror_r(int errnum, char *buf, size_t buflen)
{
  int ret = strerror_r(errnum, buf, buflen);
  if (ret != 0)
    snprintf(buf, buflen, "Error %d", errnum);
}
#elif defined(IS_GNU_STRERROR_R)
void compat_strerror_r(int errnum, char *buf, size_t buflen)
{
  char *ret = strerror_r(errnum, buf, buflen);
  if (ret != buf)
    snprintf(buf, buflen, "%s", ret);
}
#else
#error no compatible strerror_r() function found.
#endif
