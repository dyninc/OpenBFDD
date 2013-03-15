#************************************************************** 
# Copyright (c) 2010-2013, Dynamic Network Services, Inc.
# Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
# Distributed under the FreeBSD License - see LICENSE
#**************************************************************

# Checks c++ compiler flags
# $1 - the flag that will be checked, without the -.
# $2 - to execute if yes
# $3 - to execute if no
AC_DEFUN(
   [ACD_CHECK_CPP_FLAG], 
   [
      cachename=`echo $1 | sed 'yX,/=+-;{}.X___p_____X'`
      AC_CACHE_CHECK(
         does $CXX support -$1, 
         dyn_cv_prog_cpp_flag_$cachename,
         [
           AC_LANG_PUSH(C++)
           ac_save_CXXFLAGS="$CXXFLAGS"
           CXXFLAGS="$CXXFLAGS -$1"
           AC_COMPILE_IFELSE(
              [AC_LANG_PROGRAM([],[])],
              eval "dyn_cv_prog_cpp_flag_$cachename=yes",
              eval "dyn_cv_prog_cpp_flag_$cachename=no",
           )
           CXXFLAGS="$ac_save_CXXFLAGS"
           AC_LANG_POP(C++)
         ]
      )
    if eval "test \"`echo '$dyn_cv_prog_cpp_flag_'$cachename`\" = yes"; then
         :
         $2
      else
         :
         $3
      fi
   ]
)

# AHX_CONFIG_UNORDERED_MAP
# Sets up for use of the c++0x unordered_map class.
# may use tr1/unordered_map if that is not available.
# Older compilers have this as ext/hash_map in the
# __gnu_cxx::hash_map namespace
# New compilers may need -std=c++0x
# Does all required setup.
AC_DEFUN(
   [AHX_CONFIG_UNORDERED_MAP],
   [
      AC_LANG_PUSH(C++)
      AC_REQUIRE_CPP()
      AC_SUBST(HAS_UNORDERED_MAP)
      AC_SUBST(HAS_STD_UNORDERED_MAP)
      AC_SUBST(HAS_EXT_HASH_MAP)
      ac_save_CXXFLAGS="$CXXFLAGS"
      CXXFLAGS="$CXXFLAGS -Werror"
      dyn_has_unordered_map=no
      if test $dyn_has_unordered_map = no; then
         AC_MSG_CHECKING(whether the unordered_map class is available by default)
         AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM(
               [#include <unordered_map>],
               [[
                  std::unordered_map<int, int> map; 
                  map[10]=0;
               ]]
            )],   
            dyn_has_unordered_map=yes
         )
         if test $dyn_has_unordered_map = yes; then
            AC_DEFINE(HAS_STD_UNORDERED_MAP, 1, [Whether std::unordered_map is available])
         fi
         AC_MSG_RESULT($dyn_has_unordered_map)
      fi
      if test $dyn_has_unordered_map = no; then
         AC_MSG_CHECKING(whether the unordered_map class is available with c++0x)
         ac_save2_CXXFLAGS=$CXXFLAGS
         CXXFLAGS="$CXXFLAGS -std=c++0x"
         AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM(
               [#include <unordered_map>],
               [[
                  std::unordered_map<int, int> map; 
                  map[10]=0;
               ]]
            )],   
            dyn_has_unordered_map=yes
         )
         CXXFLAGS="$ac_save2_CXXFLAGS"
         if test $dyn_has_unordered_map = yes; then
            AC_DEFINE(HAS_STD_UNORDERED_MAP, 1, [Whether std::unordered_map is available])
            OTHERCXXFLAGS="$OTHERCXXFLAGS -std=c++0x"
         fi
         AC_MSG_RESULT($dyn_has_unordered_map)
      fi
      if test $dyn_has_unordered_map = no; then
         AC_MSG_CHECKING(whether the std::tr1::unordered_map class is available)
         AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM(
               [#include <tr1/unordered_map>],
               [[
                  std::tr1::unordered_map<int, int> map; 
                  map[10]=0;
               ]]
            )],   
            dyn_has_unordered_map=yes
         )
         if test $dyn_has_unordered_map = yes; then
            AC_DEFINE(HAS_TR1_UNORDERED_MAP, 1, [Should use std::tr1::unordered_map])
         fi
         AC_MSG_RESULT($dyn_has_unordered_map)
      fi

      if test $dyn_has_unordered_map = no; then
         AC_MSG_CHECKING(whether the __gnu_cxx::hash_map class is available as an unordered_map)
         AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM(
               [#include <ext/hash_map>],
               [[
                  __gnu_cxx::hash_map<int, int> map; 
                  map[10]=0;
               ]]
            )],   
            dyn_has_unordered_map=yes
         )
         if test $dyn_has_unordered_map = yes; then
            AC_DEFINE(HAS_EXT_HASH_MAP, 1, [Should use __gnu_cxx::hash_map])
         fi
         AC_MSG_RESULT($dyn_has_unordered_map)
      fi

      if test $dyn_has_unordered_map = yes; then
         AC_DEFINE(HAS_UNORDERED_MAP, 1, [Whether there is any combatable unordered_map implementation])
      fi

      AC_MSG_CHECKING(whether the unordered_map class is available at all)
      AC_MSG_RESULT($dyn_has_unordered_map)

      CXXFLAGS="$ac_save_CXXFLAGS"
      AC_LANG_POP(C++)
   ]
)

# AHX_CHECK_STRERROR_R
# Checks the vailability and type of strerror_r
# Note that checks are done under C++
AC_DEFUN(
   [AHX_CHECK_STRERROR_R],
   [
      AC_LANG_PUSH(C++)
      AC_REQUIRE_CPP()
      AC_SUBST(HAS_STRERROR_R)
      AC_SUBST(IS_ISO_STRERROR_R)
      AC_SUBST(IS_GNU_STRERROR_R)
      ac_save_CXXFLAGS="$CXXFLAGS"
      CXXFLAGS="$CXXFLAGS -Werror"
      dyn_has_strerror_r=no
      dyn_has_iso_strerror_r=no
      if test $dyn_has_strerror_r = no; then
         AC_MSG_CHECKING(whether strerror_r is available)
         AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM(
               [#include <string.h>],
               [[
                  char b[1];
                  if (strerror_r(1, b, 1)) return 0;
               ]]
            )],   
            dyn_has_strerror_r=yes
         )
         if test $dyn_has_strerror_r = yes; then
            AC_DEFINE(HAS_STRERROR_R, 1, [Whether strerror_r is available])
         fi
         AC_MSG_RESULT($dyn_has_strerror_r)
      fi
      if test $dyn_has_strerror_r = yes; then
         AC_MSG_CHECKING(whether strerror_r is the modern IEEE Std 1003.1-2001 version)
         AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM(
               [#include <string.h>],
               [[
                  int(*f)(int,char*,size_t); 
                  f=strerror_r;
                  f(0,0,0);
               ]]
            )],   
            dyn_has_iso_strerror_r=yes
         )
         if test $dyn_has_iso_strerror_r = yes; then
            AC_DEFINE(IS_ISO_STRERROR_R, 1, [Whether strerror_r is the modern IEEE Std 1003.1-2001 version])
         fi
         AC_MSG_RESULT($dyn_has_iso_strerror_r)
         if test $dyn_has_iso_strerror_r != yes; then
            dyn_has_gnu_strerror_r=no
            AC_MSG_CHECKING(whether strerror_r is the old GNU version)
            AC_COMPILE_IFELSE(
               [AC_LANG_PROGRAM(
                  [#include <string.h>],
                  [[
                     char*(*f)(int,char*,size_t); 
                     f=strerror_r;
                     f(0,0,0);
                  ]]
               )],   
               dyn_has_gnu_strerror_r=yes
            )
            if test $dyn_has_gnu_strerror_r = yes; then
               AC_DEFINE(IS_GNU_STRERROR_R, 1, [Whether strerror_r is the old GNU version])
            fi
            AC_MSG_RESULT($dyn_has_gnu_strerror_r)
         fi
      fi

      CXXFLAGS="$ac_save_CXXFLAGS"
      AC_LANG_POP(C++)
   ]
)
