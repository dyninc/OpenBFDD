/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jacob Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "standard.h"
#include "log.h"
#include <syslog.h>
#include <errno.h>


namespace openbfdd
{
  const size_t Log::MaxMessageLen = 1024;

  const char *g_LogLevelNames[] = {"none","minimal", "normal", "detailed","dev", "all"};

  // We use our own simple auto lock, since the real one uses logging ... which
  // would be a problem.  Be careful, as this class does not include the same
  // checks are the full featured one.

  class LogAutoLock
  {
  public:
    enum LockType
    {
      None, // not locked
      Read, // Read locked
      Write // write locked.
    };

    LogAutoLock(pthread_rwlock_t *lock, LogAutoLock::LockType lockInitial)

    {
      m_rwLock = lock;
      m_lockedByMeType = LogAutoLock::None;
      if (lockInitial == LogAutoLock::Read)
        ReadLock();
      else if (lockInitial == LogAutoLock::Write)
        WriteLock();
    }

    ~LogAutoLock()
    {
      if (m_lockedByMeType != LogAutoLock::None)
        UnLock();
    }

    bool ReadLock()
    {
      if (pthread_rwlock_rdlock(m_rwLock))
      {
        fprintf(stderr,  "pthread_rwlock_rdlock failed");
        return false; 
      }
      m_lockedByMeType = LogAutoLock::Read;
      return true;
    }

    bool WriteLock()
    {
      if (pthread_rwlock_wrlock(m_rwLock))
      {
        fprintf(stderr,  "pthread_rwlock_wrlock failed");
        return false; 
      }
      m_lockedByMeType = LogAutoLock::Write;
      return true;
    }

    bool UnLock()
    {

      if (pthread_rwlock_unlock(m_rwLock))
      {
        fprintf(stderr,  "pthread_rwlock_unlock failed");
        return false; 
      }
      m_lockedByMeType = LogAutoLock::None;
      return true;
    }

  private:
    pthread_rwlock_t *m_rwLock;
    LogAutoLock::LockType m_lockedByMeType;
  };


  static inline bool idLogTypeValid(Log::Type type)
  {
    return type >= 0 && type < Log::TypeCount;
  }

  Log::Log() :
  m_logFile(stderr),
  m_useSyslog(false),
  m_extendedTimeInfo(false)
  {

    if (pthread_rwlock_init(&m_settingsLock, NULL))
    {
      fprintf(stderr,  "pthread_rwlock_init failed");
      exit(1);
    }

    // Setup all types to defaults
    for (int index = 0; index < Log::TypeCount; index++)
    {
      m_types[index].enabled = false;
      m_types[index].syslogPriority = LOG_INFO;
      m_types[index].logName = "info";
      m_types[index].name = NULL;
    }

    // setup specific types 
    m_types[Critical].name = "critical";
    m_types[Critical].logName = "crit";
    m_types[Critical].syslogPriority = LOG_CRIT;

    m_types[Error].name = "error";
    m_types[Error].logName = "error";
    m_types[Error].syslogPriority = LOG_ERR;

    m_types[Warn].name = "warn";
    m_types[Warn].logName = "warn";
    m_types[Warn].syslogPriority = LOG_WARNING;

    m_types[App].name = "app";

    m_types[AppDetail].name = "app_detail";

    m_types[Session].name = "session";

    m_types[SessionDetail].name = "session_detail";
    m_types[SessionDetail].syslogPriority = LOG_DEBUG;

    m_types[Discard].name = "discard";
    m_types[Discard].logName = "discard";

    m_types[DiscardDetail].name = "discard_detail";
    m_types[DiscardDetail].logName = "discard";
    
    m_types[Packet].name = "packet";
    m_types[Packet].logName = "packet";

    m_types[PacketContents].name = "packet_contents";
    m_types[PacketContents].logName = "packet";
    m_types[PacketContents].syslogPriority = LOG_DEBUG;


    m_types[Command].name = "command";
    m_types[Command].logName = "command";

    m_types[CommandDetail].name = "command_detail";
    m_types[CommandDetail].syslogPriority = LOG_DEBUG;

    m_types[TimerDetail].name = "timer_detail";
    m_types[TimerDetail].syslogPriority = LOG_DEBUG;

    m_types[Temp].name = "temp";
    m_types[Temp].syslogPriority = LOG_DEBUG;


    SetLogLevel(Log::Normal);

    if (countof(g_LogLevelNames) != Log::LevelCount)
    {
      fprintf(stderr,  "Log Levels Mismatch");
      exit(1);
    }
  }

  Log::~Log()
  {
    closeLogFile();
    closeSyslog();

    if (pthread_rwlock_destroy(&m_settingsLock))
      fprintf(stderr,  "pthread_rwlock_destroy failed");
  }


  void Log::LogToSyslog(const char *ident, bool teeLogToStderr)
  {
    LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

    int opt = LOG_NDELAY;

    if (teeLogToStderr)
      opt |= LOG_PERROR;

    closeLogFile();
    closeSyslog();

    openlog(ident, opt, LOG_DAEMON);
    m_useSyslog = true;
    m_ident = ident;
  }


  /**
   * Closes syslog logging. 
   * Must hold write lock to call. 
   */
  void Log::closeSyslog()
  {
    if (m_useSyslog)
      closelog();
    m_useSyslog = false;

  }


  /**
   * All logging goes to the file. Stops syslog logging.
   * 
   * @param logFilePath [in] - The path of the file to log to. 
   * 
   * @return bool - false if failed to open file.  
   */
  bool Log::LogToFile(const char* logFilePath)
  {
    FILE *newFile;
    LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

    if (!logFilePath || !logFilePath[0])
    {
      closeLogFile();
      m_logFile = stderr;
      return true;
    }

    if (0 == m_logFilePath.compare(logFilePath))
      return true;

    newFile = ::fopen(logFilePath, "a");
    if (!newFile)
    {
      LogError("Failed to open logfile %s: %s", logFilePath, strerror(errno));
      return false;
    }
    closeLogFile();
    m_logFile = newFile;
    m_logFilePath = logFilePath;
    return true;
  }

  /**
   * Closes File logging. 
   * Must hold write lock to call. 
   */
  void Log::closeLogFile()
  {
    if (m_logFile && m_logFile != stderr)
      fclose(m_logFile);
    m_logFile = NULL;
    m_logFilePath.clear();
  }

  void Log::SetLogLevel(Log::Level level)
  {
    LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

    // Clear all log types.
    for (int index = 0; index < Log::TypeCount; index++)
      m_types[index].enabled = false;

    switch (level)
    {
      default:
      case None:
        break;

      case All:
        for (int index = 0; index < Log::TypeCount; index++)
          m_types[index].enabled = true;
        break;

      case Dev:
        m_types[Packet].enabled = true;
        m_types[PacketContents].enabled = true;
        m_types[AppDetail].enabled = true;
        m_types[SessionDetail].enabled = true;
        #ifdef BFD_DEBUG
        m_types[Temp].enabled = true;
        #endif
        // Fall through
      case Detailed:
        m_types[Discard].enabled = true;
        // Fall through
      case Normal:
        m_types[Session].enabled = true;
        m_types[App].enabled = true;
        m_types[Command].enabled = true;
        // Fall through
      case Minimal:
        m_types[Critical].enabled = true;
        m_types[Error].enabled = true;
        m_types[Warn].enabled = true;
        break;
    }
  }

  void Log::SetExtendedTimeInfo(bool useExtendedTime)
  {
    LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);
    m_extendedTimeInfo =  useExtendedTime;
  }



  const char *Log::LogTypeToString(Log::Type type)
  {
    // Since names never change, there is no need for lock
    if (!idLogTypeValid(type))
      return "unknown";
    return m_types[type].name;
  }

  Log::Type Log::StringToLogType(const char * str)
  {
    if (!str)
      return Log::TypeCount;

    // Since names never change, there is no need for lock
    for (int index = 0; index < Log::TypeCount; index++)
    {
      if (0==strcmp(m_types[index].name, str))
        return(Log::Type)index;
    }

    return Log::TypeCount;
  }


  const char *Log::LogLevelToString(Log::Level level)
  {
    if (level < 0 || level >= (Log::Level)countof(g_LogLevelNames))
      return "unknown";
    return g_LogLevelNames[level];
  }

  Log::Level Log::StringToLogLevel(const char * str)
  {
    if (!str)
      return Log::LevelCount;

    for (int index = 0; index < (Log::Level)countof(g_LogLevelNames); index++)
    {
      if (0==strcmp(g_LogLevelNames[index], str))
        return(Log::Level)index;
    }

    return Log::LevelCount;
  }

  void Log::EnableLogType(Log::Type type, bool enable)
  {
    LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

    if (!idLogTypeValid(type))
      return;

    m_types[type].enabled = enable;
  }

  bool Log::LogTypeEnabled(Log::Type type)
  {
    LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

    if (!idLogTypeValid(type))
      return false;

    return m_types[type].enabled;
  }



  void Log::Optional(Log::Type type, const char* format, ...) 
  {
    LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

    if (!idLogTypeValid(type))
      return;

    if (!m_types[type].enabled)
      return;

    va_list args;
    va_start(args, format);
    logMsg(m_types[type].syslogPriority, m_types[type].logName, format, args);
    va_end(args);
  }

  void Log::Message(Log::Type type, const char* format, ...) 
  {
    LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

    if (!idLogTypeValid(type))
      return;

    va_list args;
    va_start(args, format);
    logMsg(m_types[type].syslogPriority, m_types[type].logName, format, args);
    va_end(args);
  }


  /** 
   *  
   * Do the actual message format and output. 
   * Must hold at least a read lock. 
   * 
   * @param syslogPriority 
   * @param type 
   * @param format 
   * @param args 
   */
  void Log::logMsg(int syslogPriority, const char* type, const char *format, va_list args)
  {
    char message[MaxMessageLen];
    time_t now;
    struct timespec extendedNow;

    // Use CLOCK_MONOTONIC, which wil be better for "timing" but will not be useful
    // for determining the real time of events. 
    if(m_extendedTimeInfo)
    {
      if (0 > clock_gettime(CLOCK_MONOTONIC, &extendedNow))
        extendedNow.tv_sec = extendedNow.tv_nsec = 0;
    }


    vsnprintf(message, sizeof(message), format, args);

    if (m_useSyslog)
    {
      if(m_extendedTimeInfo)
      {
        syslog(syslogPriority, "[%d][%jd:%09ld] %s: %s",
                (int)getpid(),
                (intmax_t)extendedNow.tv_sec, extendedNow.tv_nsec,
                type, message);
      }
      else
      {
        syslog(syslogPriority, "[%d] %s: %s", 
               (int)getpid(), type, message);
      }
      return;
    }

    if (!m_logFile)
      return;

    now = (time_t)time(NULL);

    if(m_extendedTimeInfo)
    {

    fprintf(m_logFile, "[%u] %s[%d][%jd:%09ld] %s: %s\n", (unsigned int)now, 
            m_ident.c_str(), (int)getpid(), 
            (intmax_t)extendedNow.tv_sec, extendedNow.tv_nsec,
            type, message);
    }
    else
    {

    fprintf(m_logFile, "[%u] %s[%d] %s: %s\n", (unsigned int)now, 
            m_ident.c_str(), (int)getpid(), type, message);
    }

    fflush(m_logFile);
  }

  /**
   * Always logs message. 
   * If using syslog then this uses the LOG_WARNING level. 
   * No newline is needed. 
   */
  void Log::LogWarn(const char* format, ...) 
  {
    Log::Type type = Log::Warn;

    LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);
    va_list args;
    va_start(args, format);
    logMsg(m_types[type].syslogPriority, m_types[type].logName, format, args);
    va_end(args);
  }

  /**
   * Always logs message. 
   * If using syslog then this uses the LOG_ERR level. 
   * No newline is needed. 
   */
  void Log::LogError(const char* format, ...) 
  {
    Log::Type type = Log::Error;

    LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);
    va_list args;
    va_start(args, format);
    logMsg(m_types[type].syslogPriority, m_types[type].logName, format, args);
    va_end(args);
  }


  void Log::ErrnoError(int errnum, const char* mgs)
  {
    LogError("%s: %s", mgs, strerror(errnum) );
  }


  /**
   * Logs a message and exits the program. 
   * If using syslog then this uses the LOG_CRIT level. 
   * No newline is needed.
   */
  void Log::Fatal(const char* format, ...) 
  {
    Log::Type type = Log::Critical;

    LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);
    va_list args;
    va_start(args, format);
    logMsg(m_types[type].syslogPriority, m_types[type].logName, format, args);
    va_end(args);

    exit(1);
  }

}

