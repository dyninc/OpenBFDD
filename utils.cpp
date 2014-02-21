/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "utils.h"
#include "compat.h"
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/socket.h>

using namespace std;

static const size_t formatShortBuffersSize = 256;
static const size_t formatShortBuffersCount = 12;

static const size_t formatMediumBuffersSize = 1024;  // must be big enough for full, escaped, domain name.
static const size_t formatMediumBuffersCount = 4;
static pthread_key_t gUtilsTLSKey;
static bool gHasUtilsTLSKey = false;
static const size_t bigBufferMaxSize = 4096;


struct UtilsTLSData
{
  UtilsTLSData() :
     nextFormatShortBuffer(0),
     nextFormatMediumBuffer(0),
     bigBuffer(NULL),
     bigBufferSize(0)
  { }

  ~UtilsTLSData()
  {
    delete[] bigBuffer;
  }

  char formatShortBuffers[formatShortBuffersCount][formatShortBuffersSize];   // For "human readable" numbers. Big enough for a signed int64_t with commas.
  uint32_t nextFormatShortBuffer;

  char formatMediumBuffers[formatMediumBuffersCount][formatMediumBuffersSize]; // Big Enough for domain name as text
  uint32_t nextFormatMediumBuffer;

  char *bigBuffer;
  size_t bigBufferSize;

};


static void deleteUtilsTLS(void *data)
{
  if (data)
  {
    delete (reinterpret_cast<UtilsTLSData *>(data));
  }
}

static UtilsTLSData* getUtilsTLS()
{
  if (!gHasUtilsTLSKey)
    return NULL;
  void *data = pthread_getspecific(gUtilsTLSKey);
  if (data != NULL)
    return reinterpret_cast<UtilsTLSData *>(data);

  UtilsTLSData *tlsData = new(std::nothrow) UtilsTLSData;
  if (!tlsData)
    return NULL;

  if (0 != pthread_setspecific(gUtilsTLSKey, tlsData))
  {
    delete tlsData;
    return NULL;
  }

  return tlsData;
}



bool UtilsInit()
{
  if (gHasUtilsTLSKey)
    return true;

  int ret = pthread_key_create(&gUtilsTLSKey, deleteUtilsTLS);
  if (ret != 0)
    return false;
  gHasUtilsTLSKey = true;

  // This is a 'kludgy' attempt to detect when the pthread libraries have been
  // 'stubbed', as there are on FreeBSD without the -pthread flag.

  if (0 != pthread_setspecific(gUtilsTLSKey, (void *)(uintptr_t)(0xbefed)))
    return false;

  void *data = pthread_getspecific(gUtilsTLSKey);
  if ((uintptr_t)data != (uintptr_t)(0xbefed))
    return false;

  if (0 != pthread_setspecific(gUtilsTLSKey, NULL))
    return false;

  return true;
}

static char* nextFormatShortBuffer()
{
  UtilsTLSData *tls = getUtilsTLS();
  if (!tls)
    return NULL;

  char *nextBuf = tls->formatShortBuffers[tls->nextFormatShortBuffer++];
  if (tls->nextFormatShortBuffer >= countof(tls->formatShortBuffers))
    tls->nextFormatShortBuffer = 0;
  return nextBuf;
}


static char* nextFormatMeduimBuffer()
{
  UtilsTLSData *tls = getUtilsTLS();
  if (!tls)
    return NULL;

  char *nextBuf = tls->formatMediumBuffers[tls->nextFormatMediumBuffer++];
  if (tls->nextFormatMediumBuffer >= countof(tls->formatMediumBuffers))
    tls->nextFormatMediumBuffer = 0;
  return nextBuf;
}




/**
 * Gets the "big" buffer from tls.
 *
 * @param outBuf  [out] - Set to the buffer. Set to NULL on failure.
 *
 * @return size_t - The size of the buffer. 0 on failure.
 */
static size_t getBigBuffer(char **outBuf)
{
  if (!outBuf)
    return 0;
  *outBuf = NULL;

  UtilsTLSData *tls = getUtilsTLS();
  if (!tls)
    return 0;

  if (tls->bigBufferSize == 0)
  {
    tls->bigBuffer = new(std::nothrow) char[bigBufferMaxSize];
    if (!tls->bigBuffer)
      return 0;

    tls->bigBufferSize = bigBufferMaxSize;
  }

  *outBuf = tls->bigBuffer;
  return tls->bigBufferSize;
}

bool UtilsInitThread()
{
  char *unusedBuf;

  if (!getBigBuffer(&unusedBuf))
    return false;

  return true;
}

size_t GetBigTLSBuffer(char **outBuf)
{
  return getBigBuffer(outBuf);
}

size_t GetMediumTLSBuffer(char **outBuf)
{
  if (!outBuf)
    return 0;

  *outBuf = nextFormatMeduimBuffer();
  if (*outBuf == NULL)
    return 0;
  return formatMediumBuffersSize;
}

size_t GetSmallTLSBuffer(char **outBuf)
{
  if (!outBuf)
    return 0;

  *outBuf = nextFormatShortBuffer();
  if (*outBuf == NULL)
    return 0;
  return formatShortBuffersSize;
}


bool StringToInt(const char *arg, int64_t &value)
{
  const char *next;
  if (!StringToInt(arg, value, &next))
    return false;

  next = SkipWhite(next);

  return (*next == '\0');
}

bool StringToInt(const char *arg, int64_t &value, const char **outNext)
{
  int64_t val = 0;
  bool negative = false;
  const char *next;

  value = 0;
  if (!arg)
  {
    if (outNext)
      *outNext = NULL;
    return false;
  }

  next = SkipWhite(arg);

  if (*next == '+')
  {
    negative = false;
    next++;
  }
  else if (*next == '-')
  {
    negative = true;
    next++;
  }

  if (!isdigit(*next))
  {
    if (outNext)
      *outNext = next;
    return false;
  }

  for (; isdigit(*next); next++)
    val = val * 10 + *next - '0';

  if (outNext)
    *outNext = next;

  if (negative)
    val *= -1;
  value = val;

  if (!::isspace(*next) && *next != '\0')
    return false;

  return true;
}


bool StringToInt(const char *arg, uint64_t &value)
{
  const char *next;
  if (!StringToInt(arg, value, &next))
    return false;

  next = SkipWhite(next);

  return (*next == '\0');
}

bool StringToInt(const char *arg, uint64_t &value, const char **outNext)
{
  uint64_t val = 0;
  const char *next;

  value = 0;
  if (!arg)
  {
    if (outNext)
      *outNext = NULL;
    return false;
  }

  next = SkipWhite(arg);

  if (*next == '+')
    next++;
  else if (*next == '-')
  {
    if (outNext)
      *outNext = next;
    return false;
  }

  if (!isdigit(*next))
  {
    if (outNext)
      *outNext = next;
    return false;
  }

  for (; isdigit(*next); next++)
    val = val * 10 + *next - '0';

  if (outNext)
    *outNext = next;

  value = val;

  if (!::isspace(*next) && *next != '\0')
    return false;


  return true;
}

bool PartialStringToInt(const char *arg, uint64_t &value, const char **outNext)
{
  const char *next;
  bool ret = StringToInt(arg, value, &next);
  if (outNext)
    *outNext = next;
  if (ret)
    return true;
  // We may have failed due to non-number characters after a number, which is ok for this function.
  if (!next || *next == '\0' || next == arg)
    return false;
  // We have consumed at least one character. We want to return true if any numbers were read.
  if (isdigit(*(next - 1)))
    return true;
  return false;
}



const char*  SkipWhite(const char *str)
{
  /* EOS \0 is not a space */
  while (::isspace(*str))
    str++;
  return str;
}

char*  SkipWhite(char *str)
{
  return const_cast<char *>(SkipWhite((const char *)str));
}

const char*  SkipNonWhite(const char *str)
{
  /* EOS \0 is not a space */
  while (*str != '\0' && !::isspace(*str))
    str++;
  return str;
}

char*  SkipNonWhite(char *str)
{
  return const_cast<char *>(SkipNonWhite((const char *)str));
}

void TrimTrailingWhite(char *str)
{
  char *lastNonWhite = NULL;
  while (*str != '\0')
  {
    /* EOS \0 is not a space */
    if (!::isspace(*str))
      lastNonWhite = str;
    str++;
  }
  if (lastNonWhite != NULL && *lastNonWhite != '\0')
    lastNonWhite[1] = '\0';
}

#define NSEC_PER_SEC 1000000000L

static inline void timespecNormalize(struct timespec &ts)
{
  if (ts.tv_nsec >= NSEC_PER_SEC)
  {
    ts.tv_sec += ts.tv_nsec / (NSEC_PER_SEC);
    ts.tv_nsec = ts.tv_nsec % NSEC_PER_SEC;
  }
  else if (ts.tv_nsec <= -NSEC_PER_SEC)
  {
    ts.tv_sec += ts.tv_nsec / (NSEC_PER_SEC);
    ts.tv_nsec = -((-ts.tv_nsec) % NSEC_PER_SEC); // not sure if % is yet standardized for negatives.
  }

  // make sure both have same sign
  if (ts.tv_sec > 0 && ts.tv_nsec < 0)
  {
    ts.tv_sec--;
    ts.tv_nsec = NSEC_PER_SEC + ts.tv_nsec;
  }
  else if (ts.tv_sec < 0 && ts.tv_nsec > 0)
  {
    ts.tv_sec++;
    ts.tv_nsec = NSEC_PER_SEC - ts.tv_nsec;
  }

}

void timespecAddMs(struct timespec &ts, uint32_t ms)
{
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (ms % 1000) * 1000L * 1000;
  timespecNormalize(ts);
}

void timespecAddMicro(struct timespec &ts, uint64_t micro)
{
  ts.tv_sec += time_t(micro / 1000000);
  ts.tv_nsec += (micro % 1000000) * 1000L;
  timespecNormalize(ts);
}

int timespecCompare(const struct timespec &tsl, const struct timespec &tsr)
{
  if (tsl.tv_sec == tsr.tv_sec)
    return (tsl.tv_nsec > tsr.tv_nsec) ? 1 : (tsl.tv_nsec == tsr.tv_nsec) ? 0 : -1;

  return (tsl.tv_sec > tsr.tv_sec) ? 1 : -1;
}

void timespecSubtract(struct timespec &result, const struct timespec &tsl, const struct timespec &tsr)
{
  result.tv_sec = tsl.tv_sec - tsr.tv_sec;
  result.tv_nsec = tsl.tv_nsec - tsr.tv_nsec;
  timespecNormalize(result);
}

struct timespec timespecSubtract(const struct timespec &tsl, const struct timespec &tsr)
{
  struct timespec result;
  timespecSubtract(result, tsl, tsr);
  return result;
}


bool timespecIsNegative(const struct timespec &check)
{
  return (check.tv_sec < 0 || (check.tv_sec == 0 && check.tv_nsec < 0));
}

void timespecToTimeval(const struct timespec &src, struct timeval &dst)
{
  dst.tv_sec = src.tv_sec;
  dst.tv_usec = src.tv_nsec / 1000L;
}

struct timeval timespecToTimeval(const struct timespec &src)
{
  struct timeval dst;

  timespecToTimeval(src, dst);
  return dst;
}


void timevalToTimespec(const struct timeval &src, struct timespec &dst)
{
  dst.tv_sec = src.tv_sec;
  dst.tv_nsec = src.tv_usec * 1000L;
}

bool isTimespecEmpty(const struct timespec &src)
{
  return src.tv_sec == 0 && src.tv_nsec == 0;
}

void MilliSleep(uint32_t ms)
{
  struct timespec sleepTime;

  sleepTime.tv_sec = ms / 1000;
  sleepTime.tv_nsec = (ms % 1000) * 1000 * 1000;

  nanosleep(&sleepTime, NULL);
}

double timespecToSeconds(struct timespec time)
{
  return (double(time.tv_sec) + double(time.tv_nsec) / NSEC_PER_SEC);
}

const char* Ip4ToString(const in_addr &address)
{
  char *buf = nextFormatShortBuffer();
  if (!buf)
    return "<memerror>";

  sprintf(buf, "%hhu.%hhu.%hhu.%hhu",
          (uint8_t)((uint8_t *)&address)[0],
          (uint8_t)((uint8_t *)&address)[1],
          (uint8_t)((uint8_t *)&address)[2],
          (uint8_t)((uint8_t *)&address)[3]
         );

  return buf;
}

const char* Ip4ToString(in_addr_t address)
{
  in_addr temp;
  temp.s_addr = address;
  return Ip4ToString(temp);
}

const char* Ip4ToString(const in_addr &address, uint16_t port)
{
  return Ip4ToString(address.s_addr,  port);
}


const char* Ip4ToString(in_addr_t address, uint16_t port)
{
  char *buf = nextFormatShortBuffer();
  if (!buf)
    return "<memerror>";

  uint8_t *data = (uint8_t *)&address;


  sprintf(buf, "%hhu.%hhu.%hhu.%hhu:%hu",
          (uint8_t)data[0],
          (uint8_t)data[1],
          (uint8_t)data[2],
          (uint8_t)data[3],
          (unsigned int)port
         );

  return buf;

}




static const char *g_byteToString[] =
{
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
  "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
  "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59",
  "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
  "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", "91", "92", "93", "94", "95", "96", "97", "98", "99",
  "100", "101", "102", "103", "104", "105", "106", "107", "108", "109", "110", "111", "112", "113", "114", "115", "116", "117", "118", "119",
  "120", "121", "122", "123", "124", "125", "126", "127", "128", "129", "130", "131", "132", "133", "134", "135", "136", "137", "138", "139",
  "140", "141", "142", "143", "144", "145", "146", "147", "148", "149", "150", "151", "152", "153", "154", "155", "156", "157", "158", "159",
  "160", "161", "162", "163", "164", "165", "166", "167", "168", "169", "170", "171", "172", "173", "174", "175", "176", "177", "178", "179",
  "180", "181", "182", "183", "184", "185", "186", "187", "188", "189", "190", "191", "192", "193", "194", "195", "196", "197", "198", "199",
  "200", "201", "202", "203", "204", "205", "206", "207", "208", "209", "210", "211", "212", "213", "214", "215", "216", "217", "218", "219",
  "220", "221", "222", "223", "224", "225", "226", "227", "228", "229", "230", "231", "232", "233", "234", "235", "236", "237", "238", "239",
  "240", "241", "242", "243", "244", "245", "246", "247", "248", "249", "250", "251", "252", "253", "254", "255",
};

const char* ByteToString(uint8_t val)
{
  return  g_byteToString[val];
}


char* FormatStr(char *buffer, size_t size, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, size, format, args);
  va_end(args);
  return buffer;
}

const char* FormatShortStr(const char *format, ...)
{
  char *buf = nextFormatShortBuffer();
  if (!buf)
    return "tls_error";

  va_list args;
  va_start(args, format);
  vsnprintf(buf, formatShortBuffersSize, format, args);
  va_end(args);
  return buf;
}

const char* FormatMediumStr(const char *format, ...)
{
  char *buf = nextFormatMeduimBuffer();
  if (!buf)
    return "tls_error";

  va_list args;
  va_start(args, format);
  vsnprintf(buf, formatMediumBuffersSize, format, args);
  va_end(args);
  return buf;
}

const char* FormatMediumStrVa(const char *format, va_list args)
{
  char *buf = nextFormatMeduimBuffer();
  if (!buf)
    return "tls_error";

  vsnprintf(buf, formatMediumBuffersSize, format, args);
  return buf;
}

const char* FormatBigStr(const char *format, ...)
{
  char *buf;
  size_t bufsize = getBigBuffer(&buf);
  if (bufsize == 0)
    return "tls_error";

  va_list args;
  va_start(args, format);
  vsnprintf(buf, bufsize, format, args);
  va_end(args);
  return buf;
}

bool GetMonolithicTime(struct timespec &now)
{
  if (0 == clock_gettime(CLOCK_MONOTONIC, &now))
    return true;

  LogAssertFalse("clock_gettime failed");
  gLog.Optional(Log::Critical, "clock_gettime failed.%s", ErrnoToString());
  now.tv_sec = 0;
  now.tv_nsec = 0;
  return false;
}

bool GetRealTime(struct timespec &now)
{
  if (0 == clock_gettime(CLOCK_REALTIME, &now))
    return true;

  LogAssertFalse("clock_gettime failed");
  gLog.Optional(Log::Critical, "clock_gettime failed.%s", ErrnoToString());
  now.tv_sec = 0;
  now.tv_nsec = 0;
  return false;
}



/**
 * Helper for number format routines. buf is assumes to be big enough.
 *
 * @param val
 * @param buf - must be big enough for result. May not be null.
 * @param useCommas
 */
static void addUnsignedInt(char *buf, uint64_t val, bool useCommas)
{
  int index = 0, digits = 0;

  char c;
  char *buf2;

  // do in reverse, since that is easiest
  do
  {
    if (useCommas && digits != 0 && (digits % 3 == 0))
      buf[index++] = ',';

    buf[index++] = val % 10 + '0';
    digits++;
  }
  while ((val /= 10) > 0);

  buf[index] = '\0';

  // Reverse the string.
  buf2 = buf + index - 1;

  if (index > 1)
  {
    while (buf < buf2)
    {
      c = *buf2;
      *buf2 = *buf;
      *buf = c;
      buf++;
      buf2--;
    }
  }

}

const char* FormatInteger(uint64_t val, bool useCommas)
{
  char *buf = nextFormatShortBuffer();
  if (!buf)
    return "error";
  addUnsignedInt(buf, val, useCommas);
  return buf;
}

const char* FormatInteger(int64_t val, bool useCommas)
{
  char *buf = nextFormatShortBuffer();
  char *useBuf = buf;
  if (!buf)
    return "error";

  if (val < 0)
  {
    if (val == INT64_MIN)
    {
      // Special case since the positive of this is an overflow.
      if (useCommas)
        return "-9,223,372,036,854,775,808";
      else
        return "-9223372036854775808";
    }
    *(useBuf++) = '-';
    val = -val;
  }
  addUnsignedInt(useBuf, val, useCommas);
  return buf;
}

bool CheckArg(const char *check, const char *arg, const char **outParam)
{
  size_t len = strlen(check);

  *outParam = NULL;

  if (0 != strncmp(check, arg, len))
    return false;
  if (arg[len] == '\0')
    return true;
  if (::isspace(arg[len]))
    return (*SkipWhite(&arg[len]) == '\0');
  if (arg[len] != '=')
    return false;
  *outParam = &arg[len] + 1;

  return true;
}

/**
 * Parses a dotted quad and stops on first non-number.
 *
 * @param next [out] - On success, this will point to the character after the
 *        last ip address character.
 *
 * @param str
 * @param out_addr
 * @param next
 *
 * @return bool - false on parsing error.
 */
static bool parseIPv4Start(const char *str, uint32_t *out_addr, const char **next)
{
  uint32_t realAddr;
  uint8_t *addr = (uint8_t *)&realAddr;
  int saw_digit, octets, ch;
  uint8_t *tp;

  saw_digit = 0;
  octets = 0;
  *(tp = addr) = 0;
  while ((ch = *str++) != '\0')
  {
    if (ch >= '0' && ch <= '9')
    {
      u_int next = *tp * 10 + (ch  - '0');
      if (saw_digit && *tp == 0)
        return (0);
      if (next > 255)
        return (0);
      *tp = next;
      if (!saw_digit)
      {
        if (++octets > 4)
          return (false);
        saw_digit = 1;
      }
    }
    else if (ch == '.' && saw_digit)
    {
      if (octets == 4)
        return (false);
      *++tp = 0;
      saw_digit = 0;
    }
    else
    {
      break;
    }
  }
  if (octets < 4)
    return (false);

  *out_addr = realAddr;

  if (next)
    *next = str - 1;

  return (true);
}


bool ParseIPv4Part(const char *str, uint32_t *out_addr, const char **next /*NULL*/)
{
  const char *localNext;

  if (!parseIPv4Start(str, out_addr, &localNext))
    return false;
  if (next)
    *next = localNext;
  return true;
}

bool ParseIPv4Part(char *str, uint32_t *out_addr, char **next /*NULL*/)
{
  return ParseIPv4Part(const_cast<const char *>(str),
                       out_addr,
                       const_cast<const char **>(next)
                      );
}


bool ParseIPv4(const char *str, uint32_t *out_addr, const char **next /*NULL*/)
{
  const char *localNext;

  if (!parseIPv4Start(str, out_addr, &localNext))
    return false;
  if (localNext[0] != '\0' && !::isspace(localNext[0]))
    return false;
  if (next)
    *next = localNext;
  return true;
}

bool ParseIPv4(char *str, uint32_t *out_addr, char **next /*NULL*/)
{
  return ParseIPv4(const_cast<const char *>(str),
                   out_addr,
                   const_cast<const char **>(next)
                  );
}

bool ParseIPv4Port(const char *str, uint32_t *out_addr, uint16_t *outPort)
{
  const char *localNext;
  uint64_t port;

  if (!parseIPv4Start(str, out_addr, &localNext))
    return false;
  if (localNext[0] != ':' || ::isspace(localNext[1]))
    return false;
  localNext++;
  if (!StringToInt(localNext, port))
    return false;
  if (port > UINT16_MAX)
    return false;
  *outPort = (uint16_t)port;
  return true;
}

bool ParseIPv4Block(const char *str, uint32_t *out_addr, uint8_t *out_bits)
{
  const char *localNext;
  uint64_t bits;

  if (!parseIPv4Start(str, out_addr, &localNext))
    return false;
  if (localNext[0] != '/' || ::isspace(localNext[1]))
    return false;
  localNext++;
  if (!StringToInt(localNext, bits))
    return false;
  if (bits > 255)
    return false;
  *out_bits = (uint8_t)bits;
  return true;
}


bool ParseIPv6Part(const char *str, uint8_t *inOutStorage, const char **outNext /*NULL*/)
{
  // First determine if it is IPv4 or IPv6
  str = SkipWhite(str);
  const char *firstColon = strchr(str, ':');
  const char *last, *next;

  if (!firstColon)
    return false;

  char addrStr[INET6_ADDRSTRLEN];

  if (*str == '[')
  {
    ++str;
    last = strchr(str,  ']');
    if (!last)
      return false;
    next = last + 1;
  }
  else
  {
    // walk to find first non numeric, and non ':' and non '.' character
    for (last = str; *last != '\0'; ++last)
    {
      if (*last != ':' && *last != '.' && !isxdigit(*last))
        break;
    }
    next = last;
  }

  if (last == str || size_t(last - str) >= sizeof(addrStr))
    return false;

  memcpy(addrStr, str, last - str);
  addrStr[last - str] = '\0';
  if (1 != inet_pton(AF_INET6, addrStr, inOutStorage))
    return false;

  if (outNext)
    *outNext = next;

  return true;

}

bool ParseIPv6Part(char *str, uint8_t *inOutStorage, char **outNext /*NULL*/)
{
  return ParseIPv6Part(const_cast<const char *>(str),
                       inOutStorage,
                       const_cast<const char **>(outNext)
                      );

}


bool CheckDir(const char *dir,  int *outErrno)
{
  if (!dir || dir[0] == '\0')
  {
    if (outErrno)
      *outErrno = ENOENT;
    return false;
  }

  struct stat st;

  if (0 != stat(dir,  &st))
  {
    if (outErrno)
      *outErrno = errno;
    return false;
  }

  if (!S_ISDIR(st.st_mode))
  {
    if (outErrno)
      *outErrno = ENOTDIR;
    return false;
  }
  return true;
}

bool FileExists(const char *path, int *outErrno)
{
  if (!path || path[0] == '\0')
  {
    if (outErrno)
      *outErrno = ENOENT;
    return false;
  }

  struct stat st;

  if (0 != stat(path,  &st))
  {
    if (outErrno)
      *outErrno = errno;
    return false;
  }

  if (!S_ISREG(st.st_mode))
  {
    if (outErrno)
      *outErrno = ENOENT;
    return false;
  }
  return true;
}


bool IsExplicitRelativePath(const char *path)
{
  if (!path || !*path || path[0] != '.')
    return false;
  return (path[1] == '/'
          || (path[1] == '.' && path[2] == '/'));
}

string StripFileName(const string &path)
{
  size_t pos = path.find_last_of('/');
  if (pos == string::npos)
    return string();

  return path.substr(0, pos + 1);
}

std::string StripFilePath(const std::string &path)
{
  size_t pos = path.find_last_of('/');
  if (pos == string::npos)
    return path;

  return path.substr(pos + 1);
}

string GetCwd()
{
  // Using an allocated buffer avoids the huge stack hit, at the expense of an
  // allocation. Not sure if the tradeoff is worthwhile.
  const size_t bufSize = MAXPATHLEN;
  char *buffer = new char[bufSize];

  if (::getcwd(buffer, bufSize) == NULL)
  {
    delete[] buffer;
    return string();
  }

  string result = buffer;
  delete[] buffer;
  return result;
}


const char* SystemErrorToString(int errnum)
{
  int olderr = errno;

  char *buf = nextFormatMeduimBuffer();
  if (!buf)
  {
    errno = olderr;
    return "tls_error";
  }

  compat_strerror_r(errnum, buf, formatMediumBuffersSize);
  errno = olderr;

  return buf;
}

const char* ErrnoToString()
{
  return SystemErrorToString(errno);
}
