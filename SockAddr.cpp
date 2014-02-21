/**************************************************************
* Copyright (c) 2013, Dynamic Network Services, Inc. Jake Montgomery
* (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com) Distributed under the FreeBSD
* License - see LICENSE
***************************************************************/
#include "common.h"
#include "SockAddr.h"
#include "utils.h"
#include "lookup3.h"
#include <net/if.h>
#include <cstring>


/**
 * Sets the address to invalid, and m_addr to the default for the given type.
 *
 * @param type
 */
void sockAddrBase::init(Addr::Type type)
{
  switch (type)
  {
  case Addr::IPv4:
  {
    sockaddr_in *storage = getIPv4Storage();
    storage->sin_family = AF_INET;
    storage->sin_port = 0;
    storage->sin_addr.s_addr = INADDR_ANY;
  }
    break;
  case Addr::IPv6:
  {
    sockaddr_in6 *storage = getIPv6Storage();
    *storage = sockaddr_in6();
    storage->sin6_family = AF_INET6;
    storage->sin6_addr = in6addr_any;  /** Probably not necessary. Is  in6addr_any _guaranteed_ to be 0?*/
  }
    break;
  default:
    m_addr.ss_family = AF_UNSPEC;
    break;
  }
}

sockAddrBase::sockAddrBase(bool allowPort) : m_allowPort(allowPort)
{
  init(Addr::Invalid);
}

sockAddrBase::sockAddrBase(bool allowPort, const sockaddr_in6 *addr) : m_allowPort(allowPort)
{
  sockaddr_in6 *storage = getIPv6Storage();

  if (!addr || addr->sin6_family != AF_INET6)
  {
    init(Addr::IPv6);
    return;
  }

  memcpy(storage, addr, sizeof(sockaddr_in6));
  if (!m_allowPort)
    clearPort();
}

sockAddrBase::sockAddrBase(bool allowPort, const sockaddr_in *addr) : m_allowPort(allowPort)
{
  sockaddr_in *storage = getIPv4Storage();

  if (!addr || addr->sin_family != AF_INET)
  {
    init(Addr::IPv4);
    return;
  }

  memcpy(storage, addr, sizeof(sockaddr_in));
  if (!m_allowPort)
    clearPort();
}


sockAddrBase::sockAddrBase(bool allowPort, const in6_addr *addr) : m_allowPort(allowPort)
{
  init(Addr::IPv6);

  if (!addr)
    return;

  sockaddr_in6 *storage = getIPv6Storage();

  storage->sin6_addr = *addr;
}


sockAddrBase::sockAddrBase(bool allowPort, const in_addr *addr) : m_allowPort(allowPort)
{
  init(Addr::IPv4);

  if (!addr)
    return;

  sockaddr_in *storage = getIPv4Storage();

  storage->sin_addr.s_addr = addr->s_addr;
}

sockAddrBase::sockAddrBase(bool allowPort, Addr::Type type, in_port_t port) : m_allowPort(allowPort)
{
  init(Addr::Invalid);
  if (type != Addr::Invalid)
    SetAny(type, port);
}


sockAddrBase::sockAddrBase(bool allowPort, const sockaddr *addr, socklen_t addrlen) : m_allowPort(allowPort)
{
  if (!addr || (addrlen < sizeof(sockaddr_in) && addrlen < sizeof(sockaddr_in6)))
  {
    init(Addr::Invalid);
    return;
  }

  int family = ((sockaddr_in *)addr)->sin_family;

  if (family == AF_INET)
    init(Addr::IPv4);
  else if (family == AF_INET6)
    init(Addr::IPv6);
  else
  {
    init(Addr::Invalid);
    return;
  }

  if (addrlen < GetSize())
    return;
  memcpy(&m_addr, addr, GetSize());
  if (!m_allowPort)
    clearPort();
}

sockAddrBase::sockAddrBase(bool allowPort, const sockAddrBase &src) : m_allowPort(allowPort)
{
  copy(src);
}

sockAddrBase::sockAddrBase(bool allowPort, const char *str) : m_allowPort(allowPort)
{
  FromString(str);
}


sockAddrBase::sockAddrBase(bool allowPort, const char *str, in_port_t port) : m_allowPort(allowPort)
{
  FromString(str, port);
}


void sockAddrBase::copy(const sockAddrBase &src)
{
  if (!src.IsValid())
  {
    init(Addr::Invalid);
    return;
  }

  // Note that GetSize will work, because
  // we already made sure src was valid.
  memcpy(&m_addr, &src.m_addr, src.GetSize());
  if (!m_allowPort)
    clearPort();
}

sockAddrBase& sockAddrBase::operator = (const sockAddrBase &src)
{
  copy(src);
  return *this;
}

void sockAddrBase::SetAny(Addr::Type type, in_port_t port /*0*/)
{
  init(type);
  if (type != Addr::Invalid)
  {
    if (port != 0 && m_allowPort)
      SetPort(port);
  }
}

bool sockAddrBase::IsAny() const
{
  if (!IsValid())
    return false;

  if (IsIPv6())
  {
    // Can not use IN6_IS_ADDR_UNSPECIFIED because it breaks strict aliasing on some
    // platforms?
    const sockaddr_in6 *storage = getIPv6Storage();
    return (storage->sin6_addr.s6_addr[0] == 0
            && storage->sin6_addr.s6_addr[1] == 0
            && storage->sin6_addr.s6_addr[2] == 0
            && storage->sin6_addr.s6_addr[3] == 0
            && storage->sin6_addr.s6_addr[4] == 0
            && storage->sin6_addr.s6_addr[5] == 0
            && storage->sin6_addr.s6_addr[6] == 0
            && storage->sin6_addr.s6_addr[7] == 0
            && storage->sin6_addr.s6_addr[8] == 0
            && storage->sin6_addr.s6_addr[9] == 0
            && storage->sin6_addr.s6_addr[10] == 0
            && storage->sin6_addr.s6_addr[11] == 0
            && storage->sin6_addr.s6_addr[12] == 0
            && storage->sin6_addr.s6_addr[13] == 0
            && storage->sin6_addr.s6_addr[14] == 0
            && storage->sin6_addr.s6_addr[15] == 0);
  }
  else
    return getIPv4Storage()->sin_addr.s_addr == INADDR_ANY;
}


bool sockAddrBase::IsLinkLocal() const
{
  if (!IsValid())
    return false;

  if (IsIPv6())
    return IN6_IS_ADDR_LINKLOCAL(&getIPv6Storage()->sin6_addr);
  else
  {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&getIPv4Storage()->sin_addr.s_addr);
    return bytes[0] == 169 && bytes[1] == 254;
  }
}




void sockAddrBase::clearPort()
{
  if (IsValid())
  {
    if (IsIPv6())
      getIPv6Storage()->sin6_port = 0;
    else
      getIPv4Storage()->sin_port  = 0;
  }
}

void sockAddrBase::SetPort(in_port_t port)
{
  if (IsValid() && m_allowPort)
  {
    if (IsIPv6())
      getIPv6Storage()->sin6_port = htons(port);
    else
      getIPv4Storage()->sin_port  = htons(port);
  }
}

void sockAddrBase::SetScopIdIfLinkLocal(uint32_t id)
{
  if (!IsIPv6() || !IsLinkLocal())
    return;
  getIPv6Storage()->sin6_scope_id = id;
}

void sockAddrBase::clear()
{
  init(Addr::Invalid);
}


bool sockAddrBase::IsValid() const
{
  return (m_addr.ss_family == AF_INET6 || m_addr.ss_family == AF_INET);
}

bool sockAddrBase::IsIPv6() const
{
  return m_addr.ss_family == AF_INET6;
}

bool sockAddrBase::IsIPv4() const
{
  return m_addr.ss_family == AF_INET;
}

Addr::Type sockAddrBase::Type() const
{
  if (IsValid())
  {
    if (m_addr.ss_family == AF_INET)
      return Addr::IPv4;
    else if (m_addr.ss_family == AF_INET6)
      return Addr::IPv6;
  }
  return Addr::Invalid;

}

in_port_t sockAddrBase::Port() const
{
  uint16_t port;

  if (!IsValid() || !m_allowPort)
    return 0;

  if (IsIPv6())
    port = getIPv6Storage()->sin6_port;
  else
    port = getIPv4Storage()->sin_port;

  return ntohs(port);
}

int sockAddrBase::ProtocolFamily() const
{
  if (m_addr.ss_family == AF_INET6)
    return PF_INET6;
  else
    return PF_INET;
}


int sockAddrBase::AddressFamily() const
{
  return m_addr.ss_family;
}

socklen_t sockAddrBase::GetSize() const
{
  return AddressFamily() == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
}

/**
 * @warning does no checking at all
 */
inline sockaddr_in6* sockAddrBase::getIPv6Storage()
{
  return (sockaddr_in6 *)&m_addr;
}

/**
 * @warning does no checking at all
 */
inline sockaddr_in* sockAddrBase::getIPv4Storage()
{
  return (sockaddr_in *)&m_addr;
}

/**
 * @warning does no checking at all
 */
inline const sockaddr_in6* sockAddrBase::getIPv6Storage() const
{
  return (const sockaddr_in6 *)&m_addr;
}

/**
 * @warning does no checking at all
 */
inline const sockaddr_in* sockAddrBase::getIPv4Storage() const
{
  return (const sockaddr_in *)&m_addr;
}

const char* sockAddrBase::ToString(bool includePort /*true*/) const
{
  in_port_t port = includePort ? Port() : 0;

  if (IsIPv4())
  {
    if (port == 0)
      return Ip4ToString(getIPv4Storage()->sin_addr);
    else
      return Ip4ToString(getIPv4Storage()->sin_addr, port);
  }
  else if (IsIPv6())
  {
    const sockaddr_in6 *storage = getIPv6Storage();

    if (storage->sin6_scope_id == 0 && !port)
    {
      // Just IPv6 address
      char *buffer;
      size_t bufsize = GetSmallTLSBuffer(&buffer);
      if (!bufsize)
        return "<memerror>";
      if (!inet_ntop(AF_INET6, &storage->sin6_addr, buffer, bufsize))
        return "<Invalid Address>";
      return buffer;
    }
    else
    {
      // composite address
      char ifNameBuf[IF_NAMESIZE];
      char addrStrBuf[INET6_ADDRSTRLEN];
      const char *ifName = NULL;
      const char *addrStr = NULL;

      if (storage->sin6_scope_id)
      {
        ifName = if_indextoname(storage->sin6_scope_id, ifNameBuf);
        if (!ifName)
          ifName = "???";
      }

      addrStr = inet_ntop(AF_INET6, &storage->sin6_addr, addrStrBuf, sizeof(addrStrBuf));
      if (!addrStr)
        addrStr = "<Invalid IPv6>";

      if (port)
      {
        if (ifName)
          return FormatShortStr("[%s%%%s]:%d", addrStr, ifName, (int)port);
        else
          return FormatShortStr("[%s]:%d", addrStr, (int)port);
      }
      else
      {
        if (ifName)
          return FormatShortStr("%s%%%s", addrStr, ifName);
        else
        {
          LogVerify(false); // should never happen
          return FormatShortStr("%s", addrStr);
        }
      }
    }
  }
  else
    return "<Invalid Address>";
}

/**
 * Compares two addresses.
 * Invalid addresses always compare the same, and below anything else.
 * IPv4 addresses always compare smaller than IPv6.
 * Assumes that IPv4 Addresses are in network order.
 *
 * If is IPv6 then comparison is:
 *    sin6_addr
 *    sin6_scope_id
 *    sin6_port
 *    sin6_flowinfo
 *
 * If is IPv4 then comparison is:
 *    sin_addr
 *    sin_port
 *
 * @param rhs
 *
 * @return: -1, 0, or 1 like strcmp.
 */
int sockAddrBase::compare(const sockAddrBase &rhs, bool comparePort) const
{
  if (Type() != rhs.Type())
  {
    if (!IsValid())
      return -1;
    if (!rhs.IsValid())
      return 1;
    if (IsIPv4())
      return -1;
    // We must be IPv6
    return 1;
  }

  if (!IsValid())
    return 0;
  if (IsIPv4())
  {
    in_addr_t addrLhs = getIPv4Storage()->sin_addr.s_addr;
    in_addr_t addrRhs = rhs.getIPv4Storage()->sin_addr.s_addr;

    if (addrLhs == addrRhs)
    {
      if (!comparePort || Port() == rhs.Port())
        return 0;
      return Port() > rhs.Port() ? 1 : -1;
    }
    return ntohl(addrLhs) > ntohl(addrRhs) ? 1 : -1;
  }
  else
  {
    // IPv6
    const sockaddr_in6 *storageLhs = getIPv6Storage();
    const sockaddr_in6 *storageRhs = rhs.getIPv6Storage();
    int val = memcmp(&storageLhs->sin6_addr, &storageRhs->sin6_addr, sizeof(in6_addr));
    if (val != 0)
      return val;
    if (storageLhs->sin6_scope_id != storageRhs->sin6_scope_id)
      return ntohl(storageLhs->sin6_scope_id) > ntohl(storageRhs->sin6_scope_id) ? 1 : -1;
    if (comparePort && Port() != rhs.Port())
      return Port() > rhs.Port() ? 1 : -1;
    if (storageLhs->sin6_flowinfo != storageRhs->sin6_flowinfo)
      return ntohl(storageLhs->sin6_flowinfo) > ntohl(storageRhs->sin6_flowinfo) ? 1 : -1;
    return 0;
  }
}

/**
 * This needs a  to match compare.
 *
 * @return size_t
 */
size_t sockAddrBase::hash() const
{

  /**
   * Invalid always hashes to a constant ... 0 seems as good as any?
   */
  if (!IsValid())
    return 0;
  else if (IsIPv4())
  {
    // May not be the 'best' hash, but it should work. We add DNSP_HASHINIT
    // So that address 0.0.0.0 does not collide with !IsValid().
    if (!m_allowPort)
      return getIPv4Storage()->sin_addr.s_addr + DNSP_HASHINIT;
    else
    {
      const sockaddr_in *storage = getIPv4Storage();
      return hashlittle(&storage->sin_port, sizeof(storage->sin_port), storage->sin_addr.s_addr + DNSP_HASHINIT);
    }
  }
  else
  {
    const sockaddr_in6 *storage = getIPv6Storage();

    // Below is to make sure that we can use hashword. This should never fail, but
    // without the 'if' some compilers issue a warning.
    if (!LogVerify((sizeof(in6_addr) % 4) == 0))
      return 0;

    // May not be the 'best' hash, but it should work.
    uint32_t port = m_allowPort ? storage->sin6_port : 0;
    uint32_t hash = hashword(reinterpret_cast<const uint32_t *>(&storage->sin6_addr),
                             sizeof(in6_addr) / 4, DNSP_HASHINIT + port);
    return (hash + storage->sin6_scope_id + storage->sin6_flowinfo);
  }
}

bool sockAddrBase::FromString(const char *str)
{
  // This is called from the constructor, so it must work with an uninitialized
  // structure.
  init(Addr::Invalid);
  // First determine if it is IPv4 or IPv6
  str = SkipWhite(str);
  const char *firstColon = strchr(str, ':');
  const char *firstDot = strchr(str, '.');
  const char *firstPercent = strchr(str, '%');

  if (!firstDot && !firstColon)
    return false;
  if (!firstColon
      || (firstDot && firstDot < firstColon))
  {
    // IPv4
    uint16_t port;
    uint32_t addr;

    if (firstPercent)
      return false;

    if (!firstColon)
    {
      if (!ParseIPv4(str, &addr, &str))
        return false;
      str = SkipWhite(str);
      if (*str != '\0')
        return false;
      init(Addr::IPv4);
      getIPv4Storage()->sin_addr.s_addr = addr;
      return true;
    }
    // Has dots and a colon, presumably its an IPv4 with port
    if (!m_allowPort)
      return false;

    if (!ParseIPv4Port(str, &addr, &port))
      return false;
    init(Addr::IPv4);
    getIPv4Storage()->sin_addr.s_addr = addr;
    SetPort(port);
    return true;
  }

  // Not IPv4
  if (!firstColon || (firstPercent && (firstPercent < firstColon)))
    return false;
  else
  {
    // IPv6
    sockaddr_in6 tmp = sockaddr_in6();

    if (firstPercent || *str == '[')
    {
      char addrStr[INET6_ADDRSTRLEN];
      const char *last, *lastBrace = NULL;

      if (*str == '[')
      {
        ++str;
        last = lastBrace = strchr(str,  ']');
        if (!lastBrace)
          return false;
      }
      if (firstPercent)
      {
        if (lastBrace && lastBrace < firstPercent + 2)
          return false;
        last = firstPercent;
      }

      if (last == str || size_t(last - str) >= sizeof(addrStr))
        return false;
      memcpy(addrStr, str, last - str);
      addrStr[last - str] = '\0';
      if (1 != inet_pton(AF_INET6, addrStr, &tmp.sin6_addr))
        return false;
      if (firstPercent)
      {
        // ONLY allow interface (scope) for link local addresses (for now)
        // Otherwise address comparison breaks for regular addresses, one of which has a
        // scope.
        if (!IN6_IS_ADDR_LINKLOCAL(&tmp.sin6_addr))
          return false;

        if (lastBrace)
        {
          // already checked that (lastBrace-firstPercent >= 2)
          memcpy(addrStr, firstPercent + 1, lastBrace - (firstPercent + 1));
          addrStr[lastBrace - (firstPercent + 1)] = '\0';
          last = addrStr;
        }
        else
          last = firstPercent + 1;

        unsigned int index = if_nametoindex(last);
        if (index == 0 || index > UINT32_MAX)
          return false;
        tmp.sin6_scope_id = uint32_t(index);
      }

      if (lastBrace)
      {
        last = lastBrace + 1;
        if (*last != ':')
        {
          last = SkipWhite(last);
          if (*last != '\0')
            return false;
        }
        else
        {
          // We have a port
          uint64_t val;

          if (!m_allowPort)
            return false;
          ++last;
          if (!StringToInt(last, val))
            return false;
          if (val > UINT16_MAX)
            return false;
          tmp.sin6_port = htons(uint16_t(val));
        }
      }
    }
    else
    {
      // no [] and no % so the string can be parsed directly
      if (1 != inet_pton(AF_INET6, str, &tmp.sin6_addr))
        return false;
    }

    // Set the actual value
    init(Addr::IPv6);
    sockaddr_in6 *storage = getIPv6Storage();
    storage->sin6_addr = tmp.sin6_addr;
    storage->sin6_port = tmp.sin6_port;
    storage->sin6_scope_id = tmp.sin6_scope_id;
    return true;
  }
}


bool sockAddrBase::FromString(const char *str, in_port_t port)
{
  if (!FromString(str))
    return false;
  if (m_allowPort)
    SetPort(port);
  return true;
}
