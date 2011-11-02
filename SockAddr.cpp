/************************************************************** 
* Copyright (c) 2011, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "SockAddr.h"
#include "utils.h"
#include "lookup3.h"
#include <cstring>

using namespace std;
namespace openbfdd
{

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
    else if (Addr::IPv6) 
      return AF_INET6;
    else
      return AF_UNSPEC;
  }

  const char *Addr::TypeToString(Addr::Type type)
  {
    if (type == Addr::IPv4)
      return "IPv4";
    else if (Addr::IPv6) 
      return "IPv6";
    else
      return "<unknown>";
  }

  /**
   * Sets the address to invalid, and m_addr to the default for the given type.
   * 
   * @param type 
   */
  void sockAddrBase::init(Addr::Type type)
  {
    m_isValid = false;
    switch (type)
    {
      default:
      case Addr::IPv4:
      {
        struct sockaddr_in *storage = getIPv4Storage();
        storage->sin_family = AF_INET;
        storage->sin_port = 0;
        storage->sin_addr.s_addr = INADDR_ANY;
      }
      break;
      case Addr::IPv6:
      {
        struct sockaddr_in6 *storage = getIPv6Storage();
        memset(storage, 0, sizeof(struct sockaddr_in6));
        storage->sin6_family = AF_INET6;
        storage->sin6_addr = in6addr_any;  /** Probably not necessary. Is  in6addr_any _guaranteed_ to be 0?*/
      }
      break;
    }
  }

  sockAddrBase::sockAddrBase(bool allowPort) : m_allowPort(allowPort)
  {
    init(Addr::Invalid);
  }

  sockAddrBase::sockAddrBase(bool allowPort, const struct sockaddr_in6 *addr) : m_allowPort(allowPort)
  {
    struct sockaddr_in6 *storage = getIPv6Storage();

    if (!addr || addr->sin6_family != AF_INET6)
    {
      init(Addr::IPv6);
      return;
    }

    memcpy(storage, addr, sizeof(struct sockaddr_in6));
    if (!m_allowPort)
      clearPort();
    m_isValid = true;
  }

  sockAddrBase::sockAddrBase(bool allowPort, const struct sockaddr_in *addr) : m_allowPort(allowPort)
  {
    struct sockaddr_in *storage = getIPv4Storage();

    if (!addr || addr->sin_family != AF_INET)
    {
      init(Addr::IPv4);
      return;
    }

    memcpy(storage, addr, sizeof(struct sockaddr_in));
    if (!m_allowPort)
      clearPort();
    m_isValid = true;
  }


  sockAddrBase::sockAddrBase(bool allowPort, const struct in6_addr *addr) : m_allowPort(allowPort)
  {
    init(Addr::IPv6);

    if (!addr)
      return;

    struct sockaddr_in6 *storage = getIPv6Storage();

    storage->sin6_addr = *addr; 
    m_isValid = true;
  }


  sockAddrBase::sockAddrBase(bool allowPort, const struct in_addr *addr) : m_allowPort(allowPort)
  {
    init(Addr::IPv4);

    if (!addr)
      return;

    struct sockaddr_in *storage = getIPv4Storage();

    storage->sin_addr.s_addr = addr->s_addr; 
    m_isValid = true;
  }

  sockAddrBase::sockAddrBase(bool allowPort, Addr::Type type, in_port_t port) : m_allowPort(allowPort)
  {
    init(Addr::Invalid);
    if (type != Addr::Invalid)
      SetAny(type, port);
  }


  sockAddrBase::sockAddrBase(bool allowPort, const struct sockaddr *addr, socklen_t addrlen) : m_allowPort(allowPort)
  {
    if (!addr || (addrlen < sizeof(struct sockaddr_in) && addrlen < sizeof(struct sockaddr_in6)))
    {
      init(Addr::Invalid);
      return;
    }

    int family = ((struct sockaddr_in *)addr)->sin_family;

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
    m_isValid = true;

  }
  
  sockAddrBase::sockAddrBase(bool allowPort, const sockAddrBase &src) : m_allowPort(allowPort)  
  {
    copy(src);
  }

  sockAddrBase::sockAddrBase(bool allowPort, const char *str)  : m_allowPort(allowPort)
  {
    FromString(str);
  }


  sockAddrBase::sockAddrBase(bool allowPort, const char *str, in_port_t port) : m_allowPort(allowPort) 
  {
    FromString(str, port);
  }


  void sockAddrBase::copy(const sockAddrBase& src)
  {
    if (!src.IsValid())
    {
      init(Addr::Invalid);
      return;
    }

    m_isValid = src.m_isValid;   

    // Note that GetSize will work, because
    // we already made sure src was valid.
    memcpy(&m_addr, &src.m_addr, src.GetSize());
    if (!m_allowPort)
      clearPort();
  }

  sockAddrBase &sockAddrBase::operator=(const sockAddrBase& src)
  {
    copy(src);
    return *this;
  }

  void sockAddrBase::SetAny(Addr::Type type, in_port_t port /*0*/)
  {
    init(type);
    if (type != Addr::Invalid)
    {
      m_isValid = true;
      if (port != 0 && m_allowPort)
        SetPort(port);
    }
  }

  bool sockAddrBase::IsAny() const
  {
    if (!IsValid())
      return false;

    if(IsIPv6()) 
       return IN6_IS_ADDR_UNSPECIFIED(&getIPv6Storage()->sin6_addr);
    else
      return getIPv4Storage()->sin_addr.s_addr == INADDR_ANY;
  }


  void sockAddrBase::clearPort()
  {
    if (IsValid())
    {
      if(IsIPv6())
        getIPv6Storage()->sin6_port = 0;
      else 
        getIPv4Storage()->sin_port  = 0;
    }
  }

  void sockAddrBase::SetPort(in_port_t port)
  {
    if (IsValid() && m_allowPort)
    {
      if(IsIPv6())
        getIPv6Storage()->sin6_port = htons(port);
      else 
        getIPv4Storage()->sin_port  = htons(port);
    }
  }

  void sockAddrBase::clear()
  {
    init(Addr::Invalid);
  }


  bool sockAddrBase::IsIPv6() const
  {
    return IsValid() && m_addr.ss_family == AF_INET6;
  }

  bool sockAddrBase::IsIPv4() const
  {
    return IsValid() && m_addr.ss_family == AF_INET;
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

    if(IsIPv6())
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
    return AddressFamily() == AF_INET6 ? sizeof(struct sockaddr_in6):sizeof(struct sockaddr_in);
  }

  /**
   * @warning does not checking at all 
   */
  inline struct sockaddr_in6 *sockAddrBase::getIPv6Storage() 
  {
    return (sockaddr_in6 *)&m_addr;
  }

  /**
   * @warning does not checking at all 
   */
  inline struct sockaddr_in *sockAddrBase::getIPv4Storage() 
  {
    return (sockaddr_in *)&m_addr;
  }

  /**
   * @warning does not checking at all 
   */
  inline const struct sockaddr_in6 *sockAddrBase::getIPv6Storage() const
  {
    return (const sockaddr_in6 *)&m_addr;
  }

  /**
   * @warning does not checking at all 
   */
  inline const struct sockaddr_in *sockAddrBase::getIPv4Storage() const
  {
    return (const sockaddr_in *)&m_addr;
  }

  const char *sockAddrBase::ToString(bool includePort /*true*/) const
  {
    in_port_t port = Port();

    if (IsIPv4())
    {
      if (port == 0 || !includePort)
        return Ip4ToString(getIPv4Storage()->sin_addr);
      else
        return Ip4ToString(getIPv4Storage()->sin_addr, port);
    }
    else if (IsIPv6())
    {
      const struct sockaddr_in6 *storage = getIPv6Storage();
      char *buffer;
      size_t bufsize = GetSmallTLSBuffer(&buffer);
      if (!bufsize)
        return "<memerror>";

      if (!inet_ntop(AF_INET6, &storage->sin6_addr, buffer, bufsize))
        return "<Invalid Address>";

      if (port == 0 || !includePort)
        return buffer;

      // This uses two buffers, which is simple, but net efficient
      return FormatShortStr("[%s]:%d", buffer, (int)port);
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
        return Port() > rhs.Port() ? 1:-1;
      }
      return ntohl(addrLhs) > ntohl(addrRhs) ? 1:-1;
    }
    else
    {
      // IPv6
      const struct sockaddr_in6 *storageLhs = getIPv6Storage();
      const struct sockaddr_in6 *storageRhs = rhs.getIPv6Storage();
      int val = memcmp(&storageLhs->sin6_addr, &storageRhs->sin6_addr, sizeof(in6_addr));
      if (val != 0)
        return val;
      if (storageLhs->sin6_scope_id != storageRhs->sin6_scope_id)
        return ntohl(storageLhs->sin6_scope_id) > ntohl(storageRhs->sin6_scope_id) ? 1:-1;
      if (comparePort && Port() != rhs.Port())
        return Port() > rhs.Port() ? 1:-1;
      if (storageLhs->sin6_flowinfo != storageRhs->sin6_flowinfo)
        return ntohl(storageLhs->sin6_flowinfo) > ntohl(storageRhs->sin6_flowinfo) ? 1:-1;
      return 0;
    }
  }

  /**
   * This needs a  to match compare.
   * 
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
        return getIPv4Storage()->sin_addr.s_addr+DNSP_HASHINIT;
      else
      {
        const struct sockaddr_in *storage = getIPv4Storage();
        return hashlittle(&storage->sin_port, sizeof(storage->sin_port), storage->sin_addr.s_addr + DNSP_HASHINIT);
      }
    }
    else
    {
      const struct sockaddr_in6 *storage = getIPv6Storage();

      // Below is to make sure that we can use hashword. This should never fail, but
      // without the 'if' some compilers issue a warning.
      if (!LogVerify((sizeof(in6_addr) % 4) == 0))
        return 0; 

      // May not be the 'best' hash, but it should work.
      uint32_t port = m_allowPort ? storage->sin6_port : 0;
      uint32_t hash = hashword(reinterpret_cast<const uint32_t *>(&storage->sin6_addr), 
                                sizeof(in6_addr)/4, DNSP_HASHINIT + port);
      return (hash + storage->sin6_scope_id + storage->sin6_flowinfo);
    }
  }


  /**
   * Parses a string as a simple IPv6. 
   * Will set the address on success. 
   * No change on failure. 
   * 
   * @param str 
   * 
   * @return bool - false on failure.
   */
  bool sockAddrBase::parseIPv6(const char *str)
  {
    struct in6_addr addr;

    if (1 != inet_pton(AF_INET6, str, &addr))
      return false;

    init(Addr::IPv6);

    getIPv6Storage()->sin6_addr = addr; 
    m_isValid = true;
    return true;
  }

  bool sockAddrBase::FromString(const char *str)
  {
    init(Addr::Invalid);
    // First determine if it is IPv4 or IPv6
    str = SkipWhite(str);
    const char *firstColon = strchr(str, ':'); 
    const char *firstDot = strchr(str, '.'); 

    if (!firstDot && !firstColon)
      return false;
    if (!firstColon 
        || (firstDot && firstDot < firstColon))
    {
      // IPv4 
      uint16_t port;
      uint32_t addr;

      if (!firstColon)
      {
        if (!ParseIPv4(str, &addr, &str))
          return false;
        str = SkipWhite(str);
        if (*str != '\0')
          return false;
        init(Addr::IPv4);
        getIPv4Storage()->sin_addr.s_addr = addr;
        m_isValid = true;
        return true;
      }
      // Has dots and a colon, presumably its an IPv4 with port
      if (!m_allowPort)
        return false;

      if (!ParseIPv4Port(str, &addr, &port))
        return false;
      init(Addr::IPv4);
      getIPv4Storage()->sin_addr.s_addr = addr;
      m_isValid = true;
      SetPort(port);
      return true;
    }

    // Not IPv4
    if (!firstColon)
      return false;

    // IPv6
    if (*str == '[')
    {
      char addrStr[INET6_ADDRSTRLEN];
      uint16_t port;
      bool hasPort = false;
      struct in6_addr addr;

      ++str;
      const char *last = strchr(str,  ']');
      if (!last)
        return false;
      if (last==str || size_t(last-str) >= sizeof(addrStr))
        return false;
      memcpy(addrStr, str, last-str);
      addrStr[last-str] = '\0';
      if (1 != inet_pton(AF_INET6, addrStr, &addr))
        return false;
      if (*++last != ':')
      {
        last = SkipWhite(last);
        if (*last != '\0')
          return false;
      }
      else
      {
        // We have a port
        if (!m_allowPort)
          return false;
        uint64_t val;
        hasPort = true;
        ++last;
        if (!m_allowPort)
          return false;
        if (!StringToInt(last, val))
          return false;
        if (val > UINT16_MAX)
          return false;
        port = uint16_t(val);
      }

      init(Addr::IPv6);
      getIPv6Storage()->sin6_addr = addr; 
      m_isValid = true;
      if(hasPort)
        SetPort(port);
      return true;
    }
    else
    {
      // no [], so no port
      return parseIPv6(str);
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
}                                                                                




