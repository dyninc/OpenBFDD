/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once


struct Addr
{
  enum Type
  {
    Invalid,
    IPv4,
    IPv6
  };

  /**
   * Returns IPv4, IPv6 or <Unknown>
   *
   * @param type
   *
   * @return const char*
   */
  static const char* TypeToString(Addr::Type type);

  /**
   * Converts an address family to a Addr::Type
   */
  static Addr::Type FamilyToType(int af);

  /**
   * Converts a Addr::Type to an address family.
   */
  static int TypeToFamily(Addr::Type type);

  inline static Addr::Type TypeFromBytesLen(size_t len) { return len == 4 ? IPv4 : len == 16 ? IPv6 : Invalid;}

};
