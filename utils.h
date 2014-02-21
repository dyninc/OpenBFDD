/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Some generic utilities
#pragma once
#include <arpa/inet.h>
#include <string>
#include <cstdarg>

struct timespec;
struct timeval;
struct in_addr;

/**
 *  Note that some of these functions require the use of thread local storage.
 *  These functions require that UtilsInit() is called before using any of them.
 */


/**
* Call before using certain functions.
* Not thread safe
*
*  Note that pthread_key_delete() is never called, so this may be a very small
*  leak if used from a dynamically loaded library.
*/
bool UtilsInit();

/**
 * Optional call to pre-allocate thread local storage for the current thread.
 * This calf be used to ensure that the tls functions will not fail later.
 *
 * Call after calling UtilsInit().
 *
 * @return bool - false on failure.
 */
bool UtilsInitThread();

/**
 *
 * Checked conversion from string to  an int64_t.
 *
 * Converts the string to an int64_t. If the string is not entirely an number
 * then value is set to the partial value, and false is returned.
 *
 * Allows for trailing and leading whitespace.
 *
 * Does not check for overflow.
 *
 * @param arg [in] - The argument to parse. May be NULL.
 * @param value [out] - Set to the full or partial value. 0 is the default if no
 *              numbers can be read.
 *
 * @return int - false if the arg contains more than an integer..
 */
bool StringToInt(const char *arg, int64_t &value);


/**
 *
 * Checked conversion from string to  an int64_t.
 *
 * Converts the string to an int64_t. If the string is not entirely an number
 * then value is set to the partial value, and false is returned.
 *
 * Allows for trailing and leading whitespace.
 *
 * Does not check for overflow.
 *
 * @param arg [in] - The argument to parse. May be NULL.
 * @param value [out] - Set to the full or partial value. 0 is the default if no
 *              numbers can be read.
 * @param next [out] - This will point to the character after the
 *        last number character.
 *
 * @return int - false if the arg contains more than an integer before the space
 *         or term.
 */
bool StringToInt(const char *arg, int64_t &value, const char **next);


/**
 *
 * Checked conversion from string to  an uint64_t.
 *
 * Converts the string to an uint64_t. If the string is not entirely an number
 * then value is set to the partial value, and false is returned.
 *
 * Allows for trailing and leading whitespace.
 *
 * Does not check for overflow.
 *
 * @param arg [in] - The argument to parse. May be NULL.
 * @param value [out] - Set to the full or partial value. 0 is the default if no
 *              numbers can be read.
 *
 * @return int - false if the arg contains more than an integer..
 */
bool StringToInt(const char *arg, uint64_t &value);


/**
 *
 * Checked conversion from string to  an uint64_t.
 *
 * Converts the string to an uint64_t. If the string is not entirely an number
 * then value is set to the partial value, and false is returned.
 *
 * Allows for trailing and leading whitespace.
 *
 * Does not check for overflow.
 *
 * @param arg [in] - The argument to parse. May be NULL.
 * @param value [out] - Set to the full or partial value. 0 is the default if no
 *              numbers can be read.
 * @param next [out] - This will point to the character after the
 *        last number character.
 *
 * @return int - false if the arg contains more than an integer before the space
 *         or term.
 */
bool StringToInt(const char *arg, uint64_t &value, const char **next);


/**
 *
 * Checked conversion from string to an uint64_t, where the number may be
 * followed by non-number characters.
 *
 * Converts the string to an uint64_t. If the string is not entirely an number
 * then value is set to the partial value. If the string does not begin with a
 * number, then false is returned.
 *
 * Allows for trailing and leading whitespace.
 *
 * Does not check for overflow.
 *
 * @param arg [in] - The argument to parse. May be NULL.
 * @param value [out] - Set to the full or partial value. 0 is the default if no
 *              numbers can be read.
 * @param next [out] - This will point to the character after the
 *        last number character.
 *
 * @return int - false  the arg does not start with a number.
 */
bool PartialStringToInt(const char *arg, uint64_t &value, const char **next);


/**
 *
 *    Skips any whitespace. Returns a pointer to the first non-whitespace
 *    character.
 *
 */
const char*  SkipWhite(const char *str);
char*  SkipWhite(char *str);

/**
 *
 *    Skips any non whitespace. Returns a pointer to the first whitespace
 *    character, or the terminator
 *
 */
const char*  SkipNonWhite(const char *str);
char*  SkipNonWhite(char *str);


/**
 * Adds a '\0' after the last non-whitespace character.
 *
 * @param str
 *
 */
void TrimTrailingWhite(char *str);

/**
 * Adds the given number of milliseconds to the timespec. No overflow detection.
 *
 * @param timespec
 * @param ms
 */
void timespecAddMs(struct timespec &ts, uint32_t ms);

/**
 * Adds the given number of microseconds to the timespec. No overflow detection.
 * @note Does not handle overflow.
 *
 * @param timespec
 * @param ms
 */
void timespecAddMicro(struct timespec &ts, uint64_t micro);

/**
 * Compares two (normalized) timespec values.
 *
 * @param tsl - left compare
 * @param tsr - right compare
 *
 * @return int 1 if left is later (bigger) than right. 0 if equal, -1 if left is
 *         earlier (smaller) than right.
 */
int timespecCompare(const struct timespec &tsl, const struct timespec &tsr);

/**
 * Converts the timespec to a double (seconds).
 *
 * @param time
 *
 * @return double
 */
double timespecToSeconds(struct timespec time);

/**
 * Subtract tsr from tsl and puts the result in result.
 *
 * Overflow/underflow may be possible if extreme values are used.
 *
 * @param result [out] - Set to tsl-tsr.
 * @param tsl - value
 * @param tsr - value
 *
 */
void timespecSubtract(struct timespec &result, const struct timespec &tsl, const struct timespec &tsr);
struct timespec timespecSubtract(const struct timespec &tsl, const struct timespec &tsr);

/**
 * Checks if the given timespec contains a time less than 0.
 *
 * @param result
 * @param tsl
 * @param tsr
 *
 * @return bool
 */
bool timespecIsNegative(const struct timespec &check);

/**
 * Converts timespec to timeval
 *
 * @param src
 * @param dst
 */
void timespecToTimeval(const struct timespec &src, struct timeval &dst);

struct timeval timespecToTimeval(const struct timespec &src);

/**
 * Converts timeval timespec
 *
 * @param src
 * @param dst
 */
void timevalToTimespec(const struct timeval &src, struct timespec &dst);


/**
 * Returns true if the timespec is set to 0
 *
 * @param src
 *
 * @return bool
 */
bool isTimespecEmpty(const struct timespec &src);

/**
 * Gets the CLOCK_MONOTONIC time. On failure logs a critical message.
 * On failure sets now to 0.
 *
 * @param now
 *
 * @return bool - false on (unlikely) failure.
 */
bool GetMonolithicTime(struct timespec &now);


/**
 * Gets the CLOCK_REALTIME time. On failure logs a critical message.
 * On failure sets now to 0.
 *
 * @param now
 *
 * @return bool - false on (unlikely) failure.
 */
bool GetRealTime(struct timespec &now);

/**
 * Wrapper around nanosleep to sleep the given number of milliseconds.
 *
 * @param ms
 */
void MilliSleep(uint32_t ms);


/**
 * Same as inet_ntoa() but uses one of the buffers in thread local storage so
 * that more than one call may used in the same sprintf. (inet_ntoa is not
 * thread safe!)
 *
 * Uses one of the buffers in thread local storage so that more than one call
 * may used in the same sprintf.
 *
 * @param address
 *
 * @return const char*
 */
const char* Ip4ToString(in_addr_t address);
const char* Ip4ToString(in_addr_t address, uint16_t port);
const char* Ip4ToString(const in_addr &address);
const char* Ip4ToString(const in_addr &address, uint16_t port);


/**
 * Returns a constant string for any value up to 255.
 *
 * @return const char*
 */
const char* ByteToString(uint8_t val);


/**
 * Formats like snprintf, but returns a pointer to the buffer, making it useable
 * as a parameter in printf..
 *
 * @param format
 *
 * @return const char* - "error" in the unlikely event of a tls error.
 */
char* FormatStr(char *buffer, size_t size, const char *format, ...) ATTR_FORMAT(printf, 3, 4);


/**
 * Formats a short string using thread local storage. At most 255 characters can
 * be printed, anything beyond that will be truncated.  Do not permanently
 * store the return value .
 * This, and other functions, share tls buffers. There are at least 12 buffers
 * per thread. Do not permanently store the return value.
 *
 * @param format
 *
 * @return const char* - "tls_error" in the unlikely event of a tls error.
 */
const char *FormatShortStr(const char* format, ...)ATTR_FORMAT(printf, 1, 2);

/**
 * Formats a medium string using thread local storage. At most 1024 characters
 * can be printed, anything beyond that will be truncated.  Do not permanently
 * store the return value .
 * This, and other functions, share tls buffers. There are at least 4 buffers
 * per thread. Do not permanently store the return value.
 *
 * @param format
 *
 * @return const char* - "tls_error" in the unlikely event of a tls error.
 */
const char *FormatMediumStr(const char* format, ...)ATTR_FORMAT(printf, 1, 2);
const char *FormatMediumStrVa(const char* format, va_list args);

/**
 * Formats a "large" string using thread local storage. Currently at most 4096
 * characters can be printed, anything beyond that will be truncated.  Do not
 * permanently store the return value. Since this function uses a single tls
 * buffer, calling it again will invalidate the previous result.
 *
 * @param format
 *
 * @return const char* - "tls_error" in the unlikely event of a tls error.
 */
const char *FormatBigStr(const char* format, ...)ATTR_FORMAT(printf, 1, 2);

/**
 * Gets the large thread local storage buffer. Currently at most 4096 characters
 * Do not permanently store the return value. Since this function uses a single
 * tls buffer, calling it again will invalidate the previous result.
 *
 * @param outBuf  [out] - Set to the buffer. Set to NULL on failure.
 *
 * @return size_t - The size of the buffer. 0 on failure.
 */
size_t GetBigTLSBuffer(char **outBuf);


/**
 * Gets a medium thread local storage buffer. Currently at most 1024 characters
 * Do not permanently store the return value. There are at least 4 buffers
 * per thread. Do not permanently store the return value.
 *
 * @param outBuf  [out] - Set to the buffer. Set to NULL on failure.
 *
 * @return size_t - The size of the buffer. 0 on failure.
 */
size_t GetMediumTLSBuffer(char **outBuf);

/**
 * Gets a small thread local storage buffer. Currently at most 256 characters
 * Do not permanently store the return value. There are at least 12 buffers per
 * thread. Do not permanently store the return value.
 *
 * @param outBuf  [out] - Set to the buffer. Set to NULL on failure.
 *
 * @return size_t - The size of the buffer. 0 on failure.
 */
size_t GetSmallTLSBuffer(char **outBuf);

/**
 * Prints the integer. If useCommas is true, then it will comma separate
 * thousands to be more human readable.
 *
 * This, and other functions, share tls buffers. There are at least 12 buffers
 * per thread. Do not permanently store the return value.
 *
 * @param val
 * @param useCommas
 *
 * @return const char* - "error" in the unlikely event of a tls error.
 */
const char *FormatInteger(uint64_t val, bool useCommas = true);
const char *FormatInteger(int64_t val, bool useCommas = true);
inline const char* FormatInteger(uint32_t val, bool useCommas = true) { return FormatInteger(uint64_t(val), useCommas);}
inline const char* FormatInteger(int32_t val, bool useCommas = true) { return FormatInteger(int64_t(val), useCommas);}

/**
 *
 * Checks if the given arg begins with the given prefix ("=" not included)
 * followed by an = or only whitespace and a '\0'.
 *
 * @note, if a '=' is supplied with no following characters then outParam is set
 *  	to the '\0'.
 *
 * @param check
 * @param arg
 * @param outParam [out] - On success filled with a pointer to the part of the
 *                 param after the '=', or set to NULL if there is no '='. Set
 *                 to NULL on failure.
 *
 * @return bool - TRUE if it is a match.
 */
bool CheckArg(const char *check, const char *arg, const char **outParam);

/**
 * Parses a dotted quad optionally followed by whitespace.
 *
 * @param str
 * @param out_addr
 * @param next [out] - On success, this will point to the character after the
 *        last ip address character.
 *
 * @return bool - false on parsing error.
 */
bool ParseIPv4(const char *str, uint32_t *out_addr, const char **next = NULL);
bool ParseIPv4(char *str, uint32_t *out_addr, char **next = NULL);


/**
 * Parses a string that starts with a dotted quad
 *
 * @param str
 * @param out_addr
 * @param next [out] - On success, this will point to the character after the
 *        last ip address character.
 *
 * @return bool - false on parsing error.
 */
bool ParseIPv4Part(const char *str, uint32_t *out_addr, const char **next = NULL);
bool ParseIPv4Part(char *str, uint32_t *out_addr, char **next = NULL);

/**
 * Parses a string that starts with an IPv6 address.
 *
 * Allowable IPv6:
 *  * x:x:x:x:x:x:x:x
 *  * x:x:x:x:x:x:x:z.z.z.z
 *  * x::x:x
 *  * x::x:z.z.z.z
 *  * [x:x:x:x:x:x:x:x]
 *  * [x:x:x:x:x:x:x:z.z.z.z]
 *  * [x::x:x]
 *  * [x::x:z.z.z.z]
 *
 * @param str
 * @param inOutStorage - Must be at least 16 bytes. On success, it will be set
 *  				   to the Ipv6 data. May NOT be NULL.
 * @param outNext [out] - On success, this will point to the character after the
 *        last ip address character.
 *
 * @return bool - false on parsing error.
 */
bool ParseIPv6Part(const char *str, uint8_t *inOutStorage, const char **outNext = NULL);
bool ParseIPv6Part(char *str, uint8_t *inOutStorage, char **outNext = NULL);

/**
 * Parses a dotted quad optionally followed by a port .
 * Eg: 127.0.0.1:45
 *
 * @param str
 * @param out_addr
 * @param next
 *
 * @return bool
 */
bool ParseIPv4Port(const char *str, uint32_t *out_addr, uint16_t *outPort);

/**
 * Parses a dotted quad followed by a bitcount.
 * Like 12.43.56.78/24
 *
 * @param str
 * @param out_addr
 *
 * @return bool - false on parsing error.
 */
bool ParseIPv4Block(const char *str, uint32_t *out_addr, uint8_t *out_bits);

/**
 * Checks if the given directory exists.
 *
 * @param dir [in] - The directory to check
 * @param outErrno [out] - Set on failure.
 *
 * @return bool - true if exists.
 */
bool CheckDir(const char *dir, int *outErrno = NULL);

/**
 * Checks if the given file exists, and is a "regular" file.
 *
 * @param path
 * @param outErrno
 *
 * @return bool
 */
bool FileExists(const char *path, int *outErrno = NULL);


/**
 * Sets a variable. Used with ScopeGuard.
 */
template<typename T> void SetValue(T &ref, T val) { ref = val;}

/**
     Checks if string is a path that starts with './' or '../', or is just
     '.' or '..'
 */
bool IsExplicitRelativePath(const char *path);


/**
 * Returns the directory, given a full path. Will strip off anything after the
 * last '/'. If there is no '/' then an empty string is returned.
 *
 * @param path
 *
 * @return std::string
 */
std::string StripFileName(const std::string &path);

/**
 * Returns the filename, given a full path. Will strip off anything up to, and
 * including the last '/'. If there is no '/' then the full string is returned.
 * If the path ends in a '/', then an empty string will be returned.
 *
 * @param path
 *
 * @return std::string
 */
std::string StripFilePath(const std::string &path);

/**
 * Wrapper for ::getcwd().
 *
 *
 * @return std::string - empty on failure.
 */
std::string GetCwd();


/**
 * Like strerror, but thread safe. Uses TLS buffer.
 * This will not modify errno.
 *
 * @param errnum [in] - The error to convert.
 *
 * @return const char* [out] - A TLS buffer ... do not store.
 */
const char* SystemErrorToString(int errnum);

/**
 * Like SystemErrorToString, but uses the global errno as the error.
 * This will not modify errno.
 *
 * @return const char*
 */
const char* ErrnoToString();
