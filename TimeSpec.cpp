/**************************************************************
* Copyright (c) 2011-2017, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "TimeSpec.h"
#include "utils.h"
#include <errno.h>
#include <string.h>
#include <strings.h>

TimeSpec::TimeSpec(TimeSpec::Unit unit, int64_t value)
{
  //gLog.Optional(Log::Debug, "TimeSpec(TimeSpec::Unit)");
  switch (unit)
  {
  case Nanosec:
  default:
    // nanoseconds
    // Assuming C99 compliance for % operator.
    tv_sec = value / NSecPerSec;
    tv_nsec = value % NSecPerSec;
    break;

  case Microsec:
    // Assuming C99 compliance for % operator.
    tv_sec = value / 1000000;
    tv_nsec = (value % 1000000) * NSecPerUs;
    break;

  case Millisec:
    // Assuming C99 compliance for % operator.
    tv_sec = value / 1000;
    tv_nsec = (value % 1000) * NSecPerMs;
    break;

  case Seconds:
    tv_sec = value;
    tv_nsec = 0;
    break;

  case Minutes:
    tv_sec = value * 60;
    tv_nsec = 0;
    break;
  }
}



TimeSpec TimeSpec::MonoNow()
{
  struct TimeSpec now;

  if (0 == clock_gettime(CLOCK_MONOTONIC, &now))
    return now;

  gLog.Optional(Log::Critical, "clock_gettime(CLOCK_MONOTONIC) failed.%s", ErrnoToString());
  LogAssertFalse("clock_gettime(CLOCK_MONOTONIC) failed");
  return TimeSpec();
}

TimeSpec TimeSpec::RealNow()
{
  struct TimeSpec now;

  if (0 == clock_gettime(CLOCK_REALTIME, &now))
    return now;

  gLog.Optional(Log::Critical, "clock_gettime(CLOCK_REALTIME) failed.%s", ErrnoToString());
  LogAssertFalse("clock_gettime(CLOCK_REALTIME) failed");
  return TimeSpec();
}

/**
 * case insensitive .
 * Forgives following, but not leading whitespace.
 *
 * @param a
 * @param b
 *
 * @return bool
 */
static bool TestString(const char *testStr, const char *longStr)
{
  size_t len = strlen(testStr);
  if (0 != strncasecmp(testStr, longStr, len))
    return false;
  const char *next = longStr + len;
  while (*next != '\0')
  {
    if (!::isspace(*(next++)))
      return false;
  }
  return true;
}

TimeSpec::Unit TimeSpec::StringToUnit(const char *str)
{
  str = SkipWhite(str);

  if (TestString("nanoseconds",  str)
      || TestString("ns",  str))
    return Nanosec;

  if (TestString("microseconds",  str)
      || TestString("us",  str))
    return Microsec;

  if (TestString("milliseconds",  str)
      || TestString("ms",  str))
    return Millisec;

  if (TestString("seconds",  str)
      || TestString("sec",  str)
      || TestString("s",  str))
    return Seconds;

  if (TestString("minutes",  str)
      || TestString("min",  str)
      || TestString("m",  str))
    return Minutes;

  return None;
}

double TimeSpec::UnitToSeconds(TimeSpec::Unit unit)
{
  switch (unit)
  {
  default:
    LogAssert(false);
    return 1.0;
  case Nanosec:
    return (1.0 / NSecPerSec);

  case Microsec:
    return (1.0 / USecPerSec);

  case Millisec:
    return (1.0 / MSecPerSec);

  case Seconds:
    return 1.0;

  case Minutes:
    return 60.0;
  }
}


const char* TimeSpec::UnitToString(TimeSpec::Unit unit,  bool shortName)
{
  switch (unit)
  {
  default:
    return NULL;

  case Nanosec:
    return shortName ? "ns" : "nanoseconds";

  case Microsec:
    return shortName ? "us" : "microseconds";

  case Millisec:
    return shortName ? "ms" : "milliseconds";

  case Seconds:
    return shortName ? "sec" : "seconds";

  case Minutes:
    return shortName ? "min" : "minutes";
  }
}


const char* TimeSpec::SpanToLogText(TimeSpec::Unit unit, int decimals,  TextFlags flags)
{
  double val = ToDecimal() / UnitToSeconds(unit);

  if (0 == (flags & NoTruncation) && val == int64_t(val))
    decimals = 0;

  return FormatShortStr("%.*f %s", decimals, val,  UnitToString(unit, 0 == (flags & LongName)));
}

TimeSpec::Unit TimeSpec::SmallestSpanUnit() const
{
  double val = ToDecimal();

  if (val == 0.0)
    return TimeSpec::Seconds;

  if (val / 60 == int64_t(val / 60))
    return TimeSpec::Minutes;
  if (val < 1.0 / USecPerSec)
    return TimeSpec::Nanosec;
  if (val < 1.0 / MSecPerSec)
    return TimeSpec::Microsec;
  if (val < 1.0)
    return TimeSpec::Millisec;
  return TimeSpec::Seconds;
}

const char* TimeSpec::SpanToLogText(int decimals, TextFlags flags)
{
  return SpanToLogText(SmallestSpanUnit(),  decimals,  flags);
}

/**
 * Helper. See LocalTimeToLogText() and UTCTimeToLogText()
 */
static const char* timeToLogText(struct tm &time, const char *format)
{
  char *buffer;
  size_t bigSize = GetMediumTLSBuffer(&buffer);
  if (bigSize == 0)
    return "<tls_error>";

  if (format == NULL)
    format = "%c";

  size_t ret = strftime(buffer, bigSize, format, &time);
  if (ret == 0)
    return "<error>";
  return buffer;
}


const char* TimeSpec::LocalTimeToLogText(const char *format /*NULL*/)
{
  struct tm time;
  localtime_r(&tv_sec, &time);

  return timeToLogText(time,  format);
}

const char* TimeSpec::UTCTimeToLogText(const char *format /*NULL*/)
{
  struct tm time;
  gmtime_r(&tv_sec, &time);

  return timeToLogText(time,  format);
}
