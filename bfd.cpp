/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "standard.h"
#include "bfd.h"

// bfd constants are in a special namespace for clarity
namespace bfd
{
const char *StateNameArray[] = { "AdminDown", "Down", "Init", "Up" };
const char* StateName(bfd::State::Value state)
{
  if (state < 0 || state > bfd::State::Up)
    return "Invalid";
  return StateNameArray[state];
}


const char *DiagNameArray[] =
{
  "No Diagnostic",
  "Control Detection Time Expired",
  "Echo Function Failed",
  "Neighbor Signaled Session Down",
  "Forwarding Plane Reset",
  "Path Down",
  "Concatenated Path Down",
  "Administratively Down",
  "Reverse Concatenated Path Down"
};

const char* DiagString(Diag::Value diag)
{
  if (diag < 0 || diag > bfd::Diag::ReverseConcatPathDown)
    return "Unknown";
  return DiagNameArray[diag];

}


const char *DiagShortNameArray[] =
{
  "None",
  "Time Expired",
  "Echo Failed",
  "Neighbor Down",
  "Forwarding Reset",
  "Path Down",
  "Concat Down",
  "Admin Down",
  "Reverse Concat Down"
};

const char* DiagShortString(Diag::Value diag)
{
  if (diag < 0 || diag > bfd::Diag::ReverseConcatPathDown)
    return "Unknown";
  return DiagShortNameArray[diag];

}

} // namespace bfd
