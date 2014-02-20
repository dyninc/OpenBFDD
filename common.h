/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Common header for the beacon and control
#pragma once
#include "standard.h"
#include "log.h"

// TODO: These static port numbers are arbitrary, and can be changed if needed.
// A system should be put into place to allow these to be set.
#define PORTNUM     957
#define ALT_PORTNUM     958

namespace openbfdd
{

  /// magic message header .. in network order
  extern const uint32_t MagicMessageNumber;

  // Maximum length of a single line in the beacon->control reply.
  const size_t MaxReplyLineSize = 2046;

  // Maximum length of command from control->beacon
  const size_t MaxCommandSize = 1024; // really this is probably to large given the current command set?

  extern const char *SofwareVesrion;

  extern const char *ControlAppName;

  extern const char *BeaconAppName;

  // This is like gLog.Optional(), but does not evaluate the parameters if logging
  // is off. It incurs extra locking overhead, so only use when parameters include
  // expensive function calls.
#define LogOptional(type, format, ...) \
  do { if(gLog.LogTypeEnabled(type)) gLog.Message(type,  format,   ## __VA_ARGS__); } while(0)

  /**
   * An assertion that is thrown to the log in debug builds only. 
   */
  #ifdef BFD_DEBUG
    #ifndef BFD_SAFE_ASSERT
      // Asserts exit process
      #define LogAssert(x) \
          ((x) ? true:(gLog.Message(Log::Critical, "ASSERT FAILED: %s:%d: %s: assertion %s failed", __FILE__, __LINE__, __func__, #x), exit(1), false))
      #define LogAssertFalse(msg) \
          (gLog.Message(Log::Critical, "ASSERT FALSE: %s:%d: %s: %s", __FILE__, __LINE__, __func__, msg), exit(1), false)
      #define LogVerifyFalse(msg) \
          (gLog.Message(Log::Critical, "ASSERT FALSE: %s:%d: %s: %s", __FILE__, __LINE__, __func__, msg), exit(1), false)
      #define LogVerify(x) \
          ((x) ? true:(gLog.Message(Log::Critical, "VERIFY FAILED: %s:%d: %s: assertion %s failed", __FILE__, __LINE__, __func__, #x), exit(1), false))
    #else
      // Asserts merely log
      #define LogAssert(x) \
          ((x) ? true:(gLog.Message(Log::Critical, "ASSERT FAILED: %s:%d: %s: assertion %s failed", __FILE__, __LINE__, __func__, #x), false))
      #define LogAssertFalse(msg) \
          (gLog.Message(Log::Critical, "ASSERT FALSE: %s:%d: %s: %s", __FILE__, __LINE__, __func__, msg), false)
      #define LogVerifyFalse(msg) \
          (gLog.Message(Log::Critical, "ASSERT FALSE: %s:%d: %s: %s", __FILE__, __LINE__, __func__, msg), false)
      #define LogVerify(x) \
          ((x) ? true:(gLog.Message(Log::Critical, "VERIFY FAILED: %s:%d: %s: assertion %s failed", __FILE__, __LINE__, __func__, #x), false))
    #endif
  #else
    // Release ... no asserts.
    #define LogAssert(x) /*nothing*/
    #define LogAssertFalse(msg) /*nothing*/
    #define LogVerify(x) \
          ((x) ? true:(gLog.Message(Log::Critical, "VERIFY FAILED: %s:%d: %s: assertion %s failed", __FILE__, __LINE__, __func__, #x), false))
    #define LogVerifyFalse(msg) \
          (gLog.Message(Log::Critical, "ASSERT FALSE: %s:%d: %s: %s", __FILE__, __LINE__, __func__, msg), false)

  #endif
}
