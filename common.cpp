/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Common code for beacon and control
#include "common.h"
#include <arpa/inet.h>

namespace openbfdd
{

  const uint32_t MagicMessageNumber = htonl(0xfeed1966);
  const char *SofwareVesrion = PACKAGE_VERSION;
  const char *ControlAppName = "bfdd-control";
  const char *BeaconAppName = "bfdd-beacon";
} 






