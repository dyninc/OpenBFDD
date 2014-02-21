/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**

  wraps the various "standard" hash_map/unordered_map implementations.

 */
#pragma once

#include "config.h"

#ifndef HAS_UNORDERED_MAP
#error no unordered_map implementation found.
#endif

#ifdef HAS_STD_UNORDERED_MAP
#include <unordered_map>
#elif defined(HAS_TR1_UNORDERED_MAP)
#include <tr1/unordered_map>
#elif defined(HAS_EXT_HASH_MAP)
#include <ext/hash_map>
#endif

#ifdef  HAS_STD_UNORDERED_MAP
  template<class _Key, class _Tp,
           class _Hash = std::hash<_Key>,
           class _Pred = std::equal_to<_Key> >
  struct hash_map
  {
    typedef std::unordered_map<_Key, _Tp, _Hash, _Pred> Type;
  };

#elif defined(HAS_TR1_UNORDERED_MAP)
  template<class _Key, class _Tp,
           class _Hash = std::tr1::hash<_Key>,
           class _Pred = std::equal_to<_Key> >
  struct hash_map
  {
    typedef std::tr1::unordered_map<_Key, _Tp, _Hash, _Pred> Type;
  };
#elif defined(HAS_EXT_HASH_MAP)
  template<class _Key, class _Tp, class _HashFn = __gnu_cxx::hash<_Key>,
           class _EqualKey = std::equal_to<_Key> >
  struct hash_map
  {
    typedef __gnu_cxx::hash_map<_Key, _Tp, _HashFn, _EqualKey> Type;
  };
#else
#error no unordered_map implementation found.
#endif
