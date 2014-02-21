/**************************************************************
* Copyright (c) 2013, Dynamic Network Services, Inc. Jake Montgomery
* (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com) Distributed under the FreeBSD
* License - see LICENSE
***************************************************************/
#pragma once

#include "AddrType.h"
#include <netinet/in.h>
#include <sys/socket.h>


class IpAddr;

/**
 * Do not use this class directly. This is a base class for SockAddr and IpAddr.
 *
 * Class that can holds an IPv4 or IPv6 socket address, and optionally a port,
 * and other information. This is essentially a "struct sockaddr" a.k.a. "struct
 * sockaddr_in6" or "sockaddr_in".
 */
class sockAddrBase
{
public:
  sockAddrBase &operator=(const sockAddrBase &src);

  /**
   * @return - True if this is a valid address, but the address is 'any'. Port is
   *         not considered.
   */
  bool IsAny() const;

  /**
   * Sets the address to invalid
   */
  void clear();

  /**
   * Sets an "any" address of the given type.
   *
   * @param type - The type. if it is Invalid, then the address will be invalid.
   */
  void SetAny(Addr::Type type, in_port_t port);

  /**
   * Sets the port on an address. This may not be called if the address is
   * invalid.
   *
   * @param port [in] - The port.
   */
  void SetPort(in_port_t port);

  /**
   * Sets the ScopeId (sin6_scope_id) only if the address is already a
   * Link Local address.
   */
  void SetScopIdIfLinkLocal(uint32_t id);

  /**
   * This will be invalid only if the default constructor was used, or the
   * constructor failed, or it is a copy of another invalid address.
   */
  bool IsValid() const;

  /**
   * Checks if it is an ipv6 address
   * If this is invalid then this returns false.
   *
   * @return bool
   */
  bool IsIPv6() const;

  /**
   * Checks if it is an ipv4 address.
   * If this is invalid then this returns false.
   *
   * @return bool
   */
  bool IsIPv4() const;

  /**
   * Checks for IPv4 or IPv6 link local address.
   */
  bool IsLinkLocal() const;

  /**
   * Gets the type.
   *
   * @return Addr::Type
   */
  Addr::Type Type() const;

  /**
   * Is the port field non-zero?
   *
   * @return bool
   */
  bool HasPort() const { return Port() != 0;}

  /**
   * @return in_port_t - The port, or 0 if this is not valid, or port was never
   *         set.
   */
  in_port_t Port() const;

  /**
   * Gets the PF_ constant for the type.
   * Even invalid addresses will be PF_INET6 or  PF_INET
   *
   * @return int
   */
  int ProtocolFamily() const;

  /**
   * Gets the AF_ constant for the type
   *
   * Even invalid addresses will be AF_INET6 or  AF_INET
   *
   * @return int
   */
  int AddressFamily() const;

  /**
   * Returns the sockaddr. If no port has been set, then it will be
   * 0. If no address has been set, or the address is invalid, then it will be
   * in6addr_any or INADDR_ANY depending on the type.
   *
   * @return const struct sockaddr&
   */
  const sockaddr& GetSockAddr() const { return *(sockaddr *)&m_addr;}

  /**
   * Gets the size of the data returned by GetSockAddr()
   *
   * @return socklen_t
   */
  socklen_t GetSize() const;


  /**
   * Prints a string representation of the address.
   * If the port is not 0, then it will be included.
   *
   * @note UtilsInit() must be called before using.
   *
   * @return const char* - A temporary buffer. do not
   */
  const char* ToString(bool includePort = true) const;

  /**
   * This can be used with hash_map
   */
  size_t hash() const;

  /**
   * Sets the address from a string.
   * If the string contains a port, and this does not allow ports, then it is a
   * failure.
   *
   * Allowable IPv6:
   *  * x:x:x:x:x:x:x:x
   *  * x::x:x  (any number of ::)
   *  * [x:x:x:x:x:x:x:x]:port
   *  * [x::x:x]:port
   *  * [x::x:x]
   * Also IPv6 allowable ONLY if address is link local (iface is interface name):
   *  * x:x:x:x:x:x:x:x%iface
   *  * x::x:x%iface  (any number of ::)
   *  * [x:x:x:x:x:x:x:x%iface]:port
   *  * [x::x:x%iface]:port
   *  * [x::x:x%iface]
   *
   * Allowable IPv4:
   *  * xxx.xxx.xxx.xxx
   *  * xxx.xxx.xxx.xxx:port
   *
   * @param str
   *
   * @return bool - false if the string can not be read. This will be Invalid on
   *         failure.
   */
  bool FromString(const char *str);

  /**
   * Sets the address from a string, and sets the port.
   * @note the port is set to port even if the string specifies a different port.
   *
   * @param str
   * @param port
   *
   * @return bool - false if the string can not be read. This will be Invalid on
   *         failure.
   */

  bool FromString(const char *str, in_port_t port);


protected:
  int compare(const sockAddrBase &rhs, bool comparePort) const;

protected:
  /**
   * Creates an invalid structure.
   */
  explicit sockAddrBase(bool allowPort);

  /**
   * Create from a string. See SetFromString().
   */
  sockAddrBase(bool allowPort, const char *str);

  /**
   * Create from a string, and force the port. See SetFromString().
   */
  sockAddrBase(bool allowPort, const char *str, in_port_t port);

  /**
   * Assumes all information is filled out and valid.
   * That means there is a port.
   */
  sockAddrBase(bool allowPort, const sockaddr *addr, socklen_t addrlen);

  /**
   * Assumes all information is filled out and valid.
   * That means there is a port.
   */
  sockAddrBase(bool allowPort, const sockaddr_in6 *addr);

  /**
   * Assumes all information is filled out and valid.
   * That means there is a port.
   */
  sockAddrBase(bool allowPort, const sockaddr_in *addr);

  sockAddrBase(bool allowPort, const in6_addr *addr);

  sockAddrBase(bool allowPort, const in_addr *addr);

  /**
   * Creates a socket with the "Any" address.
   */
  sockAddrBase(bool allowPort, Addr::Type type, in_port_t port);

  sockAddrBase(bool allowPort, const sockAddrBase &src);



private:
  void init(Addr::Type type);
  void copy(const sockAddrBase &src);
  void clearPort();

  sockaddr_in6* getIPv6Storage();
  const sockaddr_in6* getIPv6Storage() const;
  sockaddr_in* getIPv4Storage();
  const sockaddr_in* getIPv4Storage() const;

  sockaddr_storage m_addr;
  const bool m_allowPort;
};



/**
 * Class that can holds an IPv4 or IPv6 socket address, and optionally a port,
 * and other information. This is essentially a "struct sockaddr" a.k.a. "struct
 * sockaddr_in6" or "sockaddr_in"
 */
class SockAddr : public sockAddrBase
{
public:

  /**
   * Creates an invalid structure.
   */
  SockAddr() : sockAddrBase(true) { }

  /**
   * Create from a string. See SetFromString().
   */
  explicit SockAddr(const char *str) : sockAddrBase(true, str) { }

  /**
   * Create from a string, and force the port. See SetFromString().
   */
  SockAddr(const char *str, in_port_t port) : sockAddrBase(true, str, port) { }

  /**
   * Assumes all information is filled out and valid.
   * That means there is a port.
   */
  SockAddr(const sockaddr *addr, socklen_t addrlen) : sockAddrBase(true, addr, addrlen) { }

  /**
   * Assumes all information is filled out and valid.
   * That means there is a port.
   */
  explicit SockAddr(const sockaddr_in6 *addr) : sockAddrBase(true, addr) { }

  /**
   * Assumes all information is filled out and valid.
   * That means there is a port.
   */
  explicit SockAddr(const sockaddr_in *addr) : sockAddrBase(true, addr) { }

  explicit SockAddr(const in6_addr *addr) : sockAddrBase(true, addr) { }
  explicit SockAddr(const in_addr *addr) : sockAddrBase(true, addr) { }

  SockAddr(const SockAddr &src) : sockAddrBase(true,  src) { }

  /**
   * Creates a socket with the "Any" address.
   * If port is 0 then HasPort() will be false.
   */
  explicit SockAddr(Addr::Type type, in_port_t port = 0) : sockAddrBase(true, type, port) { }

  /**
   * If port is 0 then HasPort() will be false.
   */
  explicit SockAddr(const IpAddr &src, in_port_t port = 0) : sockAddrBase(true,  (const sockAddrBase &)src) { if (port)
      SetPort(port);}

  /**
   * Sets an "any" address of the given type.
   *
   * @param type - The type. if it is Invalid, then the address will be invalid.
   */
  void SetAny(Addr::Type type, in_port_t port = 0) { sockAddrBase::SetAny(type,  port);}

  SockAddr &operator=(const SockAddr &src) { sockAddrBase::operator =(src); return *this;}

  bool operator==(const SockAddr &other) const { return 0 == compare(other,  true);}
  bool operator!=(const SockAddr &other) const { return 0 != compare(other,  true);}
  bool IsEqualExceptPort(const SockAddr &other) const { return 0 == compare(other,  false);}

  /**
   * Comparison class for STL containers.
   */
  struct LessClass
  {bool operator ()(const SockAddr &lhs, const SockAddr &rhs) const { return lhs.compare(rhs,  true) < 0;}
  };

  /**
   * Comparison class for STL containers, ignores port.
   */
  struct LessNoPortClass
  {bool operator ()(const SockAddr &lhs, const SockAddr &rhs) const { return lhs.compare(rhs,  false) < 0;}
  };
};


/**
 * This is a subclass of sockAddrBase that never has a port.
 * It is more than just the base IP address, at least for IPv6, so that it can
 * contain things like the sin6_scope_id.
 * This is a bit kludgy.
 */
class IpAddr : public sockAddrBase
{
public:
  /**
   * Creates an invalid structure.
   */

  IpAddr() : sockAddrBase(false) { }

  /**
   * Create from a string. See SetFromString().
   */
  explicit IpAddr(const char *str) : sockAddrBase(false, str) { }


  /**
   * Assumes all information is filled out and valid.
   */
  IpAddr(const sockaddr *addr, socklen_t addrlen) : sockAddrBase(false, addr, addrlen) { }

  /**
   * Assumes all information is filled out and valid.
   */
  explicit IpAddr(const sockaddr_in6 *addr) : sockAddrBase(false, addr) { }

  /**
   * Assumes all information is filled out and valid. of course, port is ignored.
                                                          */
  explicit IpAddr(const sockaddr_in *addr) : sockAddrBase(false, addr) { }

  explicit IpAddr(const in6_addr *addr) : sockAddrBase(false, addr) { }
  explicit IpAddr(const in_addr *addr) : sockAddrBase(false, addr) { }

  /**
   * Creates a socket with the "Any" address.
   */
  explicit IpAddr(Addr::Type type) : sockAddrBase(false, type, 0) { }

  IpAddr(const IpAddr &src) : sockAddrBase(false, src) { }

  explicit IpAddr(const SockAddr &src) : sockAddrBase(false, src) { }

  IpAddr &operator=(const IpAddr &src) { sockAddrBase::operator =(src); return *this;}

  void SetAny(Addr::Type type) { sockAddrBase::SetAny(type,  0);}

  const char* ToString() const { return sockAddrBase::ToString(false);}

  bool FromString(const char *str) { return sockAddrBase::FromString(str);}

  bool operator==(const IpAddr &other) const { return 0 == compare(other,  false);}
  bool operator!=(const IpAddr &other) const { return 0 != compare(other,  false);}

  /**
   * Comparison class for STL containers.
   */
  struct LessClass
  {bool operator ()(const IpAddr &lhs, const IpAddr &rhs) const { return lhs.compare(rhs,  false) < 0;}
  };

private:
  // Hide Port related functions (they would just be ignored anyway)
  void SetPort(in_port_t port);
  bool HasPort() const;
  in_port_t Port() const;
  bool FromString(const char *str, in_port_t port);
};
