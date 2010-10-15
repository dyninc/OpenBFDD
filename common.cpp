/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Common code for beacon and control
#include "common.h"

namespace openbfdd
{
  static uint8_t tempMagicMessageNumber[4] = {0xfe, 0xed, 0x19, 0x66};
  const uint32_t MagicMessageNumber = *(uint32_t *)tempMagicMessageNumber;
  const char *SofwareVesrion = PACKAGE_VERSION;
  const char *ControlAppName = "bfdd-control";
  const char *BeaconAppName = "bfdd-beacon";
} 






