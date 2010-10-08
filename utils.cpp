/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Base header for the beacon and control
#include "common.h"
#include "utils.h"
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

using namespace std;

namespace openbfdd 
{
  static const size_t formatShortBuffersSize = 256;
  static pthread_key_t gUtilsTLSKey;
  static bool gHasUtilsTLSKey = false;
  static const size_t bigBufferMaxSize = 4096;

  struct UtilsTLSData
  {
    UtilsTLSData() : 
      nextFormatShortBuffer(0),
      bigBuffer(NULL),
      bigBufferSize(0)
    {}

    ~UtilsTLSData()
    {
      delete[] bigBuffer;
    }

    char formatShortBuffers[20][formatShortBuffersSize]; // For "human readable" numbers. Big enough for a signed int64_t with commas.
    uint32_t nextFormatShortBuffer; 

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

  static UtilsTLSData *getUtilsTLS()
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
    return true;
  }

  static char *nextFormatShortBuffer()
  {
    UtilsTLSData *tls = getUtilsTLS();
    if(!tls)
      return NULL;

    char *nextBuf = tls->formatShortBuffers[tls->nextFormatShortBuffer++];
    if(tls->nextFormatShortBuffer >= countof(tls->formatShortBuffers))
      tls->nextFormatShortBuffer = 0;
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
    if(!tls)
      return 0;

    if(tls->bigBufferSize == 0)
    {
      tls->bigBuffer = new(std::nothrow) char[bigBufferMaxSize];
      if(!tls->bigBuffer)
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

  bool StringToInt(const char *arg, int64_t &value)
  {
    int64_t val = 0;
    bool negative = false;
    const char *next;

    value = 0;
    if (!arg)
      return false;

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
      return false;

    for (; isdigit(*next); next++)
      val = val*10 + *next - '0';

    if (negative)
      val *= -1;
    value = val;

    next = SkipWhite(next);

    return(*next == '\0');
  }


  /** skip whitespace, return new pointer into string */
  const char*  SkipWhite(const char* str)
  {
    /* EOS \0 is not a space */
    while ( ::isspace(*str) )
      str++;
    return str;
  }

#define NSEC_PER_SEC 1000000000L

  static inline void timespecNormalize(struct timespec &ts)
  {
    if (ts.tv_nsec >= NSEC_PER_SEC)
    {
      ts.tv_sec += ts.tv_nsec/(NSEC_PER_SEC);
      ts.tv_nsec = ts.tv_nsec%NSEC_PER_SEC;
    }
    else if (ts.tv_nsec <= -NSEC_PER_SEC)
    {
      ts.tv_sec += ts.tv_nsec/(NSEC_PER_SEC);
      ts.tv_nsec = -((-ts.tv_nsec)%NSEC_PER_SEC); // not sure if % is yet standardized for negatives.
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
    ts.tv_sec += ms/1000;
    ts.tv_nsec += (ms%1000)*1000L*1000;
    timespecNormalize(ts);
  }

  void timespecAddMicro(struct timespec &ts, uint64_t micro)
  {
    ts.tv_sec += time_t(micro/1000000);
    ts.tv_nsec += (micro%1000000)*1000L;
    timespecNormalize(ts);
  }

  int timespecCompare(const struct timespec &tsl, const struct timespec &tsr)
  {
    if (tsl.tv_sec == tsr.tv_sec)
      return(tsl.tv_nsec > tsr.tv_nsec) ? 1 : (tsl.tv_nsec == tsr.tv_nsec) ? 0:-1;

    return(tsl.tv_sec > tsr.tv_sec) ? 1 : -1;
  }

  void timespecSubtract(struct timespec &result, const struct timespec &tsl, const struct timespec &tsr)
  {
    result.tv_sec = tsl.tv_sec - tsr.tv_sec;
    result.tv_nsec = tsl.tv_nsec - tsr.tv_nsec;
    timespecNormalize(result);
  }


  bool timespecIsNegative(struct timespec &check)
  {
    return(check.tv_sec < 0 || (check.tv_sec == 0 && check.tv_nsec < 0));
  }

  void MilliSleep(uint32_t ms)
  {
    struct timespec sleepTime;

    sleepTime.tv_sec = ms/1000;
    sleepTime.tv_nsec = (ms %1000)*1000*1000;

    nanosleep(&sleepTime, NULL);
  }

  const char *Ip4ToString(struct in_addr &address)
  {
    char * buf = nextFormatShortBuffer();
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

  const char *Ip4ToString(in_addr_t address)
  {
    in_addr temp;
    temp.s_addr = address;
    return Ip4ToString(temp);
  }
       
  const char *Ip4ToString(struct in_addr &address, uint16_t port)
  {
    return Ip4ToString(address.s_addr,  port);
  }


  const char *Ip4ToString(in_addr_t address, uint16_t port)
  {
    char * buf = nextFormatShortBuffer();
    if (!buf)
      return "<memerror>";

    sprintf(buf, "%hhu.%hhu.%hhu.%hhu:%hu", 
            (uint8_t)((uint8_t *)&address)[0],
            (uint8_t)((uint8_t *)&address)[1],
            (uint8_t)((uint8_t *)&address)[2],
            (uint8_t)((uint8_t *)&address)[3],
            (unsigned int)port
           );

    return buf;

  }




  static const char *g_byteToString[] = 
  {
    "0","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19",
    "20","21","22","23","24","25","26","27","28","29","30","31","32","33","34","35","36","37","38","39",
    "40","41","42","43","44","45","46","47","48","49","50","51","52","53","54","55","56","57","58","59",
    "60","61","62","63","64","65","66","67","68","69","70","71","72","73","74","75","76","77","78","79",
    "80","81","82","83","84","85","86","87","88","89","90","91","92","93","94","95","96","97","98","99",
    "100","101","102","103","104","105","106","107","108","109","110","111","112","113","114","115","116","117","118","119",
    "120","121","122","123","124","125","126","127","128","129","130","131","132","133","134","135","136","137","138","139",
    "140","141","142","143","144","145","146","147","148","149","150","151","152","153","154","155","156","157","158","159",
    "160","161","162","163","164","165","166","167","168","169","170","171","172","173","174","175","176","177","178","179",
    "180","181","182","183","184","185","186","187","188","189","190","191","192","193","194","195","196","197","198","199",
    "200","201","202","203","204","205","206","207","208","209","210","211","212","213","214","215","216","217","218","219",
    "220","221","222","223","224","225","226","227","228","229","230","231","232","233","234","235","236","237","238","239",
    "240","241","242","243","244","245","246","247","248","249","250","251","252","253","254","255",
  };

  const char *ByteToString(uint8_t val)
  {
    return  g_byteToString[val];
  }


  char *FormatStr(char *buffer, size_t size, const char* format, ...) 
  {
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, size, format, args);
    va_end(args);
    return buffer;
  }

  const char *FormatShortStr(const char* format, ...) 
  {
    char * buf = nextFormatShortBuffer();
    if (!buf)
      return "tls_error";

    va_list args;
    va_start(args, format);
    vsnprintf(buf, formatShortBuffersSize, format, args);
    va_end(args);
    return buf;
  }

  const char *FormatBigStr(const char* format, ...) 
  {
    char * buf;
    size_t bufsize = getBigBuffer(&buf);
    if (bufsize==0)
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
    gLog.Message(Log::Critical, "clock_gettime failed. BFD may not function correctly : %s", strerror(errno));
    now.tv_sec = 0;
    now.tv_nsec = 0;
    return false;
  }


  /**
   * Helper for number format routines. buf is assumes to be big enough. 
   * 
   * @param val 
   * @param useCommas 
   */
  static void addUnsignedInt( char *buf, uint64_t val, bool useCommas)
  {
    int index = 0, digits = 0;

    char c;
    char *buf2;

    // do in reverse, since that is easiest
    do    
    {
      if(useCommas && digits != 0 && (digits%3 == 0))
         buf[index++] = ',';

      buf[index++] = val % 10 + '0';   
      digits++;
    } while ((val /= 10) > 0);  

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

  const char *FormatInteger(uint64_t val, bool useCommas)
  {
    char * buf = nextFormatShortBuffer();
    if (!buf)
      return "error";
    addUnsignedInt(buf, val, useCommas);
    return buf;
  }

  const char *FormatInteger(int64_t val, bool useCommas)
  {
    char * buf = nextFormatShortBuffer();
    char * useBuf = buf;
    if (!buf)
      return "error";

    if(val< 0)
    {
      if(val == INT64_MIN)
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

}







