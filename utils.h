/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Some generic utilities
#pragma once
#include <arpa/inet.h>

struct timespec;
struct in_addr;

/**
 *  Note that some of these functions require the use of thread local storage. 
 *  These functions require that UtilsInit() is called before using any of them. 
 */ 

namespace openbfdd
{ 
  /**
   * Call before using certain functions. 
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
   *    Skips any whitespace. Returns a pointer to the first non-whitespace
   *    character.
   *
   */
  const char*  SkipWhite(const char* str);


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

  /**
   * Checks if the given timespec contains a time less than 0.
   * 
   * @param result 
   * @param tsl 
   * @param tsr 
   * 
   * @return bool 
   */
  bool timespecIsNegative(struct timespec &check);


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
   * Wrapper around nanosleep to sleep the given number of milliseconds.
   * 
   * @param ms 
   */
  void MilliSleep(uint32_t ms);

  
  /**       
   * 
   * Same as inet_ntoa() but uses one of the buffers in thread local storage so 
   * that more than one call may used in the same sprintf. (inet_ntoa is not 
   * thread safe!) 
   * 
   * @param address 
   * 
   * @return const char* 
   */
  const char *Ip4ToString(in_addr_t address);
  const char *Ip4ToString(in_addr_t address, uint16_t port);
  const char *Ip4ToString(struct in_addr &address);
  const char *Ip4ToString(struct in_addr &address, uint16_t port);


  /**
   * Returns a constant string for any value up to 255.
   * 
   * @return const char* 
   */
  const char *ByteToString(uint8_t val);

  
  /**
   * Formats like snprintf, but returns a pointer to the buffer, making it useable 
   * as a parameter in printf.. 
   * 
   * @param format 
   * 
   * @return const char* 
   */
  char *FormatStr(char *buffer, size_t size, const char* format, ...) ATTR_FORMAT(printf, 3, 4);


  /**
   * Formats a short string using thread local storage. At most 255 characters can 
   * be printed, anything beyond that will be truncated. 
   * This, and other functions, share tls buffers. There are at least 16 buffers 
   * per thread. Do not permanently store the return value. 
   * 
   * @param format 
   * 
   * @return const char* - "tls_error" in the unlikely event of a tls error.
   */
  const char *FormatShortStr(const char* format, ...) ATTR_FORMAT(printf, 1, 2);

  /**
   * Formats a "large" string using thread local storage. Currently most 4096 
   * characters can be printed, anything beyond that will be truncated.  Do not 
   * permanently store the return value. 
   * Since this function uses a single tls buffer, calling it again from the same
   * thread will invalidate the previous result. 
   * 
   * @param format 
   * 
   * @return const char* - "tls_error" in the unlikely event of a tls error.
   */
  const char *FormatBigStr(const char* format, ...) ATTR_FORMAT(printf, 1, 2);

  /**
   * Prints the integer. If useCommas is true, then it will comma separate 
   * thousands to be more human readable. 
   *  
   * This, and other functions, share tls buffers. There are at least 16 buffers 
   * per thread. Do not permanently store the return value.
   * 
   * @param val 
   * @param useCommas 
   * 
   * @return const char* - "error" in the unlikely event of a tls error.
   */
  const char *FormatInteger(uint64_t val, bool useCommas = true);
  const char *FormatInteger(int64_t val, bool useCommas = true);
  inline const char *FormatInteger(uint32_t val, bool useCommas = true) {return FormatInteger(uint64_t(val),useCommas);}
  inline const char *FormatInteger(int32_t val, bool useCommas = true) {return FormatInteger(int64_t(val),useCommas);} 
  
  
};






