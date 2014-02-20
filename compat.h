/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once

/**
 * Compatibility routines.
 */

/**
 * Like the POSIX compliant strerror_r. Except that, on failure, the message
 * "Error XXXX" will be put into buf (or as much of it as will fit.)
 * This function may change errno.
 *
 * @return int
 */
void compat_strerror_r(int errnum, char *buf, size_t buflen);
