#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
sinclude(acx_nlnetlabs.m4)

AC_INIT(openbfdd, 0.4.1)
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_SRCDIR([BeaconMain.cpp])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_LANG(C++)

AC_SUBST(WARNINGCXXFLAGS)
AC_SUBST(OTHERCXXFLAGS)
                    
ACD_CHECK_CPP_FLAG(Wall, [WARNINGCXXFLAGS="$WARNINGCXXFLAGS -Wall"])
ACD_CHECK_CPP_FLAG(Wextra, [WARNINGCXXFLAGS="$WARNINGCXXFLAGS -Wextra"])
                                            
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

# no kevent mode
AC_ARG_ENABLE(kevent, AC_HELP_STRING([--disable-kevent], [Use select() instead of kevent() based scheduler, even on systems that support kevent.]))
kevent_enabled="$enable_kevent"
AC_SUBST(kevent_enabled)
case "$kevent_enabled" in
        no)
		AC_DEFINE([NO_KEVENT_SCHEDULER], 1, [define this to disable use of the kevent scheduler.])
		;;
	yes|*)
		# nothing to do.
		;;
esac



# Checks for libraries.


# Checks for header files.
AC_CHECK_HEADERS([syslog.h])

# Checks for typedefs, structures, and compiler characteristics.
ACX_CHECK_FORMAT_ATTRIBUTE
ACX_CHECK_UNUSED_ATTRIBUTE

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([kevent select])

AC_SEARCH_LIBS([clock_gettime],[rt posix4])
AC_CHECK_FUNCS([clock_gettime])

AH_BOTTOM(
AHX_CONFIG_FORMAT_ATTRIBUTE
AHX_CONFIG_UNUSED_ATTRIBUTE

#if defined(HAVE_KEVENT) && !(defined NO_KEVENT_SCHEDULER)
#    define USE_KEVENT_SCHEDULER
#endif

)

AC_CONFIG_FILES([Makefile])
AC_CONFIG_HEADER([config.h])
AC_OUTPUT
