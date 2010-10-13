#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
sinclude(acx_nlnetlabs.m4)

AC_INIT(openbfdd, 0.2.1)
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_SRCDIR([BeaconMain.cpp])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_LANG(C++)

AC_CHECK_FUNCS([vector])

AC_SUBST(WARNINGCXXFLAGS)
AC_SUBST(OTHERCXXFLAGS)
                    
ACD_CHECK_CPP_FLAG(Wall, [WARNINGCXXFLAGS="$WARNINGCXXFLAGS -Wall"])
ACD_CHECK_CPP_FLAG(Wextra, [WARNINGCXXFLAGS="$WARNINGCXXFLAGS -Wextra"])

AC_MSG_RESULT($OTHERCXXFLAGS)
                                            
AHX_CONFIG_UNORDERED_MAP

# debug mode
AC_ARG_ENABLE(debug, AC_HELP_STRING([--enable-debug], [Enable debug warnings and asserts]))
debug_enabled="$enable_debug"
AC_SUBST(debug_enabled)
case "$enable_debug" in
        yes)
		AC_DEFINE([BFD_DEBUG], [], [define this to enable debug checks.])
		;;
	no|*)
		# nothing to do.
		;;
esac

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h netinet/in.h sys/socket.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
ACX_CHECK_FORMAT_ATTRIBUTE
ACX_CHECK_UNUSED_ATTRIBUTE
AC_HEADER_STDBOOL
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([kevent memset select socket strcasecmp strdup strerror])

AC_SEARCH_LIBS([clock_gettime],[rt posix4])
AC_CHECK_FUNCS([clock_gettime])

AH_BOTTOM(
AHX_CONFIG_FORMAT_ATTRIBUTE
AHX_CONFIG_UNUSED_ATTRIBUTE
)

AC_CONFIG_FILES([Makefile])
AC_CONFIG_HEADER([config.h])
AC_OUTPUT
