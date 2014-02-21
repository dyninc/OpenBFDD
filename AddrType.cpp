/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "AddrType.h"
#include <sys/socket.h>

using namespace std;

Addr::Type Addr::FamilyToType(int af)
{
  if (af == AF_INET)
    return Addr::IPv4;
  else if (af == AF_INET6)
    return Addr::IPv6;
  else
    return Addr::Invalid;
}

int Addr::TypeToFamily(Addr::Type type)
{
  if (type == Addr::IPv4)
    return AF_INET;
  else if (type == Addr::IPv6)
    return AF_INET6;
  else
    return AF_UNSPEC;
}

const char* Addr::TypeToString(Addr::Type type)
{
  if (type == Addr::IPv4)
    return "IPv4";
  else if (type == Addr::IPv6)
    return "IPv6";
  else
    return "<unknown>";
}
