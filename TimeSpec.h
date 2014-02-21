/**************************************************************
* Copyright (c) 2011, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once

#include <time.h>

/**
 * Minimal wrapper around struct timespec.
 */
struct TimeSpec : public timespec
{
  static const long NSecPerSec = 1000000000L;
  static const long NSecPerMs =  1000000L;
  static const long USecPerMs =  1000L;

  enum Unit
  {
    None,
    Microsec,
    Millisec,
    Seconds,
    Minutes
  };

  TimeSpec() { tv_sec = tv_nsec = 0;}
  explicit TimeSpec(const timeval &val) { tv_sec = val.tv_sec; tv_nsec = val.tv_usec * 1000L;}
  explicit TimeSpec(double sec)  { tv_sec = time_t(sec); tv_nsec = long((sec - tv_sec) * 1000000000L);}
  TimeSpec(time_t sec, long nsec) { tv_sec = sec; tv_nsec = nsec;}
  TimeSpec(const timespec &src) : timespec(src) { }
  // Does not check for overflow!
  // Use caution, to avoid getting TimeSpec(time_t sec, long nsec) instead
  TimeSpec(Unit unit, int64_t value);

  /**
   * Gets CLOCK_MONOTONIC
   *
   * @return TimeSpec
   */
  static TimeSpec MonoNow();

  /**
   * Gets CLOCK_REALTIME
   *
   * @return TimeSpec
   */
  static TimeSpec RealNow();

  /**
   * Test for time is 0 seconds.
   */
  bool empty() { return tv_sec == 0 && tv_nsec == 0;}

  /**
   * Test for negative time value. Does not need to be normalized
   */
  bool IsNegative()
  {
    if (tv_sec > 0 && tv_nsec > 0)
      return false;
    if (tv_sec < 0 && tv_nsec < 0)
      return true;

    // Use compare, which normalizes, so is a bit more expensive
    return *this < TimeSpec();
  }


  /**
   * Set to 0 seconds.
   */
  void clear() { tv_sec = 0; tv_nsec = 0;}

  /**
   * Roll tv_nsec in tv_sec if it is above (or below) NSecPerSec
   */
  void Normalize()
  {
    if (tv_nsec >= NSecPerSec)
    {
      tv_sec += tv_nsec / NSecPerSec; tv_nsec = tv_nsec % NSecPerSec;
    }
    else if (tv_nsec <= -NSecPerSec)
    {
      tv_sec += tv_nsec / NSecPerSec; tv_nsec = -((-tv_nsec) % NSecPerSec);
    } // not sure if % is yet standardized for negatives.

    // make sure both have same sign
    if (tv_sec > 0 && tv_nsec < 0)
    {
      tv_sec--; tv_nsec = NSecPerSec + tv_nsec;
    }
    else if (tv_sec < 0 && tv_nsec > 0)
    {
      tv_sec++; tv_nsec = NSecPerSec - tv_nsec;
    }
  }

  double ToDecimal() const
  {
    return (double(tv_sec) + double(tv_nsec) / NSecPerSec);
  }

  TimeSpec &operator+=(const struct timespec &rhs)
  {
    tv_sec += rhs.tv_sec;
    tv_nsec += rhs.tv_nsec;
    Normalize();
    return *this;
  }

  TimeSpec operator+(const struct timespec &rhs) const
  {
    return TimeSpec(*this) += rhs;
  }


  TimeSpec &operator-=(const struct timespec &rhs)
  {
    tv_sec -= rhs.tv_sec;
    tv_nsec -= rhs.tv_nsec;
    Normalize();
    return *this;
  }

  TimeSpec operator-(const struct timespec &rhs) const
  { return TimeSpec(*this) -= rhs;}

  /**
   * No check for overflow. Assumes normalized.
   */
  TimeSpec &operator*=(long mult)
  {
    int64_t nsec = int64_t(tv_nsec) * mult;
    tv_sec *= mult;

    // Since we use a temporary (64 bit) value for nsec, we must manually normalize
    if (nsec >= NSecPerSec)
    {
      tv_sec += nsec / NSecPerSec; nsec = nsec % NSecPerSec;
    }
    else if (nsec <= -NSecPerSec)
    {
      tv_sec += nsec / NSecPerSec; nsec = -((-nsec) % NSecPerSec);
    } // not sure if % is yet standardized for negatives.

    tv_nsec = nsec;

    // make sure both have same sign
    if (tv_sec > 0 && tv_nsec < 0)
    {
      tv_sec--; tv_nsec = NSecPerSec + tv_nsec;
    }
    else if (tv_sec < 0 && tv_nsec > 0)
    {
      tv_sec++; tv_nsec = NSecPerSec - tv_nsec;
    }

    return *this;
  }


  TimeSpec operator*(long mult) const
  { return TimeSpec(*this) *= mult;}


  /**
   * No check for overflow. Assumes normalized.
   */
  TimeSpec &operator*=(double mult)
  {
    double nsec = tv_nsec * mult;
    double sec = tv_sec * mult;

    tv_sec = time_t(sec);
    sec -= tv_sec;
    nsec += (sec * NSecPerSec);
    if (nsec > NSecPerSec)
    {
      time_t addSec = time_t(nsec / NSecPerSec);
      nsec -= double(addSec) * NSecPerSec;
      tv_sec += addSec;
    }

    tv_nsec = (long)nsec;
    Normalize();

    return *this;
  }

  TimeSpec operator*(double mult) const
  { return TimeSpec(*this) *= mult;}

  TimeSpec &operator/=(long div)
  {
    tv_nsec = (tv_nsec / div) + long(int64_t(tv_sec % div) * NSecPerSec / div);
    tv_sec = tv_sec / div;
    Normalize();
    return *this;
  }

  TimeSpec operator/(long div) const
  { return TimeSpec(*this) /= div;}

  inline bool IsNormaized()
  {
    return ((tv_sec >= 0 && tv_nsec >= 0 && tv_nsec < NSecPerSec)
            || (tv_sec <= 0 && tv_nsec <= 0 && tv_nsec > -NSecPerSec));
  }

  bool operator<(const timespec &rhs) const
  {
    // does not assume normalized, which complicates a bit.
    // This is a lazy implementation. A more efficient one could be written if
    // needed.
    TimeSpec trhs(rhs);
    TimeSpec tlhs(*this);
    trhs.Normalize();
    tlhs.Normalize();
    return (tlhs.tv_sec < trhs.tv_sec || (tlhs.tv_sec == trhs.tv_sec && tlhs.tv_nsec < trhs.tv_nsec));
  }

  bool operator<=(const timespec &rhs) const
  {
    // does not assume normalized, which complicates a bit.
    // This is a lazy implementation. A more efficient one could be written if
    // needed.
    TimeSpec trhs(rhs);
    TimeSpec tlhs(*this);
    trhs.Normalize();
    tlhs.Normalize();
    return (tlhs.tv_sec < trhs.tv_sec || (tlhs.tv_sec == trhs.tv_sec && tlhs.tv_nsec <= trhs.tv_nsec));
  }

  bool operator>(const timespec &rhs) const
  {
    // does not assume normalized, which complicates a bit.
    // This is a lazy implementation. A more efficient one could be written if
    // needed.
    TimeSpec trhs(rhs);
    TimeSpec tlhs(*this);
    trhs.Normalize();
    tlhs.Normalize();
    return (tlhs.tv_sec > trhs.tv_sec || (tlhs.tv_sec == trhs.tv_sec && tlhs.tv_nsec > trhs.tv_nsec));
  }

  bool operator>=(const timespec &rhs) const
  {
    // does not assume normalized, which complicates a bit.
    // This is a lazy implementation. A more efficient one could be written if
    // needed.
    TimeSpec trhs(rhs);
    TimeSpec tlhs(*this);
    trhs.Normalize();
    tlhs.Normalize();
    return (tlhs.tv_sec > trhs.tv_sec || (tlhs.tv_sec == trhs.tv_sec && tlhs.tv_nsec >= trhs.tv_nsec));
  }


  bool operator==(const timespec &rhs) const
  {
    // does not assume normalized, which complicates a bit.
    // This is a lazy implementation. A more efficient one could be written if
    // needed.
    TimeSpec trhs(rhs);
    TimeSpec tlhs(*this);
    trhs.Normalize();
    tlhs.Normalize();
    return (tlhs.tv_sec == trhs.tv_sec && tlhs.tv_nsec == trhs.tv_nsec);
  }

  bool operator!=(const timespec &rhs) const
  {
    return !operator==(rhs);
  }


  /**
   * Converts a string to a unit.
   * Forgives whitespace.
   *
   * @param str
   *
   * @return Unit - None on error.
   */
  static Unit StringToUnit(const char *str);

  /**
   * Converts a unit to string.
   *
   * @param unit
   *
   * @return const char* - NULL if the uint is invalid, including "None".
   */
  static const char* UnitToString(TimeSpec::Unit unit, bool shortName = true);

  /**
   * Number of seconds in this unit.
   *
   * @param unit
   *
   * @return double
   */
  static double UnitToSeconds(TimeSpec::Unit unit);

  /**
   * Converts to a string with a  single value and single unit.
   * Uses utils.h TLS buffer for result.
   *
   * @param unit
   * @param decimals [in] - The maximum number of decimal palaces to use.
   * @param shortName
   *
   * @return const char*
   */
  const char* SpanToLogText(TimeSpec::Unit unit, int decimals, bool shortName = true);

  /**
   * Converts to a string with a single value and single unit. The unit is chosen
   * based on the value.
   * Uses utils.h TLS buffer for result.
   *
   *
   * @param shortName
   * @param decimals [in] - The maximum number of decimal palaces to use.
   *
   * @return const char*
   */
  const char* SpanToLogText(int decimals, bool shortName = true);


  /**
   * Formats the time, in local time.
   * Uses utils.h TLS buffer for result.
   *
   * @note - Fractions of a second are dropped.
   *
   * @param format [in] - A format like std::strftime(). NULL to use default time
   *               format.
   *
   * @return const char* - The resulting string, of "<error>" on failure
   */
  const char* LocalTimeToLogText(const char *format = NULL);


  /**
   * Formats the time, in UTC time.
   * Uses utils.h TLS buffer for result.
   *
   * @note - Fractions of a second are dropped.
   *
   * @param format [in] - A format like std::strftime(). NULL to use default time
   *               format.
   *
   * @return const char* - The resulting string, of "<error>" on failure
   */
  const char* UTCTimeToLogText(const char *format = NULL);

};
