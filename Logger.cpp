/**************************************************************
* Copyright (c) 2010-2014, Dynamic Network Services, Inc.
* Author - Jake Montgomery (jmontgomery@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "standard.h"
#include "Logger.h"
#include "LogException.h"
#include "compat.h"
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

const size_t Logger::MaxMessageLen;


// We use our own simple auto lock, since the real one uses logging ... which
// would be a problem.  Be careful, as this class does not include the same
// checks as the full featured one.
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
      fprintf(stderr,  "pthread_rwlock_rdlock failed\n");
      return false;
    }
    m_lockedByMeType = LogAutoLock::Read;
    return true;
  }

  bool WriteLock()
  {
    if (pthread_rwlock_wrlock(m_rwLock))
    {
      fprintf(stderr,  "pthread_rwlock_wrlock failed\n");
      return false;
    }
    m_lockedByMeType = LogAutoLock::Write;
    return true;
  }

  bool UnLock()
  {

    if (pthread_rwlock_unlock(m_rwLock))
    {
      fprintf(stderr,  "pthread_rwlock_unlock failed\n");
      return false;
    }
    m_lockedByMeType = LogAutoLock::None;
    return true;
  }

private:
  pthread_rwlock_t *m_rwLock;
  LogAutoLock::LockType m_lockedByMeType;
};


Logger:: Logger() :
   m_types(NULL)
   , m_typeCount(Log::TypeCount)
   , m_levelsMap(NULL)
   , m_levelCount(Log::LevelCount)
   , m_logFile(NULL)
   , m_useSyslog(false)
   , m_extendedTimeInfo(TimeInfo::None)
{

  if (pthread_rwlock_init(&m_settingsLock, NULL))
  {
    fprintf(stderr,  "pthread_rwlock_init failed\n");
    exit(1);
  }

  // Create the levels and types. Yes, this may leak a little if a memory
  // exception is thrown.
  m_levelsMap = new LevelInfo[m_levelCount];
  m_types = new TypeInfo[m_typeCount];

  for (size_t i = 0; i < m_levelCount; ++i)
  {
    m_levelsMap[i].types = new bool[m_typeCount];
    m_levelsMap[i].name = "unknown";

  }

  // Setup all types to defaults
  for (size_t index = 0; index < m_typeCount; index++)
  {
    m_types[index].enabled = false;
    m_types[index].throws = false;
    m_types[index].syslogPriority = LOG_INFO;
    m_types[index].logName = "info";
    m_types[index].name = "xxdummyxxx";
    m_types[index].description = NULL;
    m_types[index].toStdErr = false;
    m_types[index].toStdOut = false;
  }

}

Logger::~Logger()
{
  closeLogFile();
  closeSyslog();

  for (size_t i = 0; i < m_levelCount; ++i)
    delete[] m_levelsMap[i].types;
  delete[] m_levelsMap;
  delete[] m_types;

  if (pthread_rwlock_destroy(&m_settingsLock))
    fprintf(stderr,  "pthread_rwlock_destroy failed\n");
}


inline bool Logger::isLogTypeValid(Log::Type type)
{
  return type >= 0 && static_cast<size_t>(type) < m_typeCount;
}

inline bool Logger::isLogLevelValid(Log::Level level)
{
  return level >= 0 && static_cast<size_t>(level) < m_levelCount;
}

void Logger::copyLevelTypes(Log::Level source, Log::Level dest)
{
  if (!isLogLevelValid(source) || !isLogLevelValid(dest))
    return;
  memcpy(m_levelsMap[dest].types, m_levelsMap[source].types, sizeof(bool) * m_typeCount);
}

void Logger::setLevelTypes(Log::Level level, bool value)
{
  if (!isLogLevelValid(level))
    return;
  memset(m_levelsMap[level].types, value ? 1 : 0, sizeof(bool) * m_typeCount);
}

void Logger::LogToSyslog(const char *ident, bool teeLogToStderr)
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
void Logger::closeSyslog()
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
bool Logger::LogToFile(const char *logFilePath)
{
  FILE *newFile;
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

  if (!logFilePath || !logFilePath[0])
  {
    closeLogFile();
    m_logFile = NULL;
    return true;
  }

  if (0 == m_logFilePath.compare(logFilePath))
    return true;

  newFile = ::fopen(logFilePath, "a");
  if (!newFile)
  {
    char buf[1024];
    compat_strerror_r(errno, buf, sizeof(buf));
    fprintf(stderr, "Failed to open logfile %s: %s\n", logFilePath, buf);
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
void Logger::closeLogFile()
{
  if (m_logFile)
    fclose(m_logFile);
  m_logFile = NULL;
  m_logFilePath.clear();
}

void Logger::SetLogLevel(Log::Level level)
{
  if (!isLogLevelValid(level))
    return;

  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);


  // Set all log types.
  for (size_t index = 0; index < m_typeCount; index++)
    m_types[index].enabled = isTypeInLevel((Log::Type)index, level);
}


void Logger::SetStdOut(Log::Type type, bool shouldOut)
{
  if (!isLogTypeValid(type))
    return;

  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

  m_types[type].toStdOut =  shouldOut;
}


void Logger::SetStdOut(Log::Level level, bool shouldOut)
{
  if (!isLogLevelValid(level))
    return;

  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

  for (size_t index = 0; index < m_typeCount; index++)
  {
    if (isTypeInLevel((Log::Type)index, level))
      m_types[index].toStdOut = shouldOut;
  }
}

void Logger::SetStdErr(Log::Type type, bool shouldOut)
{
  if (!isLogTypeValid(type))
    return;

  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

  m_types[type].toStdErr =  shouldOut;

}

void Logger::SetStdErr(Log::Level level, bool shouldOut)
{
  if (!isLogLevelValid(level))
    return;

  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

  for (size_t index = 0; index < m_typeCount; index++)
  {
    if (isTypeInLevel((Log::Type)index, level))
      m_types[index].toStdErr = shouldOut;
  }
}

void Logger::SetExtendedTimeInfo(TimeInfo::Type type)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);
  m_extendedTimeInfo =  type;
}

void Logger::SetPrintTypeNames(bool printTypeNames)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);
  m_printTypeNames =  printTypeNames;
}

const char* Logger::LogTypeToString(Log::Type type)
{
  // Since names never change, there is no need for lock
  if (!isLogTypeValid(type))
    return "unknown";
  return m_types[type].name;
}

const char* Logger::LogTypeDescription(Log::Type type)
{
  // Since names never change, there is no need for lock
  if (!isLogTypeValid(type))
    return "";
  if (m_types[type].description == NULL)
    return "";
  return m_types[type].description;
}

Log::Type Logger::StringToLogType(const char *str)
{
  if (!str)
    return Log::TypeCount;

  // Since names never change, there is no need for lock
  for (size_t index = 0; index < m_typeCount; index++)
  {
    if (0 == strcmp(m_types[index].name, str))
      return(Log::Type)index;
  }

  return Log::TypeCount;
}


const char* Logger::LogLevelToString(Log::Level level)
{
  if (!isLogLevelValid(level))
    return "unknown";
  return m_levelsMap[level].name.c_str();
}

Log::Level Logger::StringToLogLevel(const char *str)
{
  if (!str)
    return Log::LevelCount;

  for (size_t index = 0; index < m_levelCount; index++)
  {
    if (0 == strcmp(m_levelsMap[index].name.c_str(), str))
      return(Log::Level)index;
  }

  return Log::LevelCount;
}

void Logger::EnableLogType(Log::Type type, bool enable)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

  if (!isLogTypeValid(type))
    return;

  m_types[type].enabled = enable;
}

bool Logger::LogTypeEnabled(Log::Type type)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

  if (!isLogTypeValid(type))
    return false;

  return m_types[type].enabled;
}



void Logger::Optional(Log::Type type, const char *format, ...)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

  if (!isLogTypeValid(type))
    return;

  if (!m_types[type].enabled)
    return;

  va_list args;
  va_start(args, format);
  logMsg(&m_types[type], format, args);
  va_end(args);
}

void Logger::OptionalVa(Log::Type type, const char *format, va_list args)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

  if (!isLogTypeValid(type))
    return;

  if (!m_types[type].enabled)
    return;

  logMsg(&m_types[type], format, args);
}


void Logger::Message(Log::Type type, const char *format, ...)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

  if (!isLogTypeValid(type))
    return;

  va_list args;
  va_start(args, format);
  logMsg(&m_types[type], format, args);
  va_end(args);
}

void Logger::MessageVa(Log::Type type, const char *format, va_list args)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

  if (!isLogTypeValid(type))
    return;

  logMsg(&m_types[type], format, args);
}


/**
 *
 * Do the actual message format and output.
 * Must hold at least a read lock.
 *
 */
void Logger::logMsg(const TypeInfo *typeInfo, const char *format, va_list args)
{
  char message[MaxMessageLen];
  char timeStr[64];

  vsnprintf(message, sizeof(message), format, args);

  if (typeInfo->throws)
    throw (LogException(message));

  if (m_extendedTimeInfo == TimeInfo::Real)
  {
    struct timespec extendedNow;

    if (0 > clock_gettime(CLOCK_REALTIME, &extendedNow))
      extendedNow.tv_sec = extendedNow.tv_nsec = 0;

    snprintf(timeStr, sizeof(timeStr), "[%jd:%09ld]", (intmax_t)extendedNow.tv_sec, extendedNow.tv_nsec);
  }
  else if (m_extendedTimeInfo == TimeInfo::Mono)
  {
    struct timespec extendedNow;

    if (0 > clock_gettime(CLOCK_MONOTONIC, &extendedNow))
      extendedNow.tv_sec = extendedNow.tv_nsec = 0;

    snprintf(timeStr, sizeof(timeStr), "[%jd:%09ld]", (intmax_t)extendedNow.tv_sec, extendedNow.tv_nsec);
  }
  else
    timeStr[0] = '\0';

  const char *logName = "";
  const char *logNamePrefix = "";
  if (m_printTypeNames)
  {
    logName = typeInfo->logName;
    logNamePrefix = " ";
  }

  if (m_useSyslog)
  {
    syslog(typeInfo->syslogPriority, "[%d]%s%s%s: %s",
           (int)getpid(),
           timeStr,
           logNamePrefix, logName, message);
  }

  if (!m_logFile && !typeInfo->toStdErr && !typeInfo->toStdOut)
    return;


  if (m_logFile)
  {
    time_t now = (time_t)time(NULL);

    fprintf(m_logFile, "[%u] %s[%d]%s%s%s: %s\n", (unsigned int)now,
            m_ident.c_str(), (int)getpid(),
            timeStr,
            logNamePrefix, logName, message);

    fflush(m_logFile);
  }

  if (typeInfo->toStdErr || typeInfo->toStdOut)
  {
    for (int i = 0; i < 2; ++i)
    {
      FILE *dst;
      if (i == 0 && typeInfo->toStdErr)
        dst = stderr;
      else if (i == 0 && typeInfo->toStdOut)
        dst = stdout;
      else
        continue;

      // no pid for stderr and stdout
      if (!m_ident.empty() || timeStr[0] != '\0')
      {
        fprintf(dst, "%s%s%s%s: %s\n",
                m_ident.c_str(),
                timeStr,
                logNamePrefix, logName, message);
      }
      else if (m_printTypeNames)
        fprintf(dst, "%s: %s\n", logName, message);
      else
        fprintf(dst, "%s\n", message);
    }
  }

}

/**
 * Always logs message.
 * If using syslog then this uses the LOG_WARNING level.
 * No newline is needed.
 */
void Logger::LogWarn(const char *format, ...)
{
  Log::Type type = Log::Warn;

  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);
  va_list args;
  va_start(args, format);
  logMsg(&m_types[type], format, args);
  va_end(args);
}

/**
 * Always logs message.
 * If using syslog then this uses the LOG_ERR level.
 * No newline is needed.
 */
void Logger::LogError(const char *format, ...)
{
  Log::Type type = Log::Error;

  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);
  va_list args;
  va_start(args, format);
  logMsg(&m_types[type], format, args);
  va_end(args);
}


void Logger::ErrnoError(int errnum, const char *mgs)
{
  char buf[1024];
  compat_strerror_r(errnum, buf, sizeof(buf));
  LogError("%s: (%d) %s", mgs, errnum, buf);
}


/**
 * Logs a message and exits the program.
 * If using syslog then this uses the LOG_CRIT level.
 * No newline is needed.
 */
void Logger::Fatal(const char *format, ...)
{
  Log::Type type = Log::Critical;

  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);
  va_list args;
  va_start(args, format);
  logMsg(&m_types[type], format, args);
  va_end(args);

  exit(1);
}


void Logger::ThrowOnLogType(Log::Type type, bool shouldThrow)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Write);

  if (!isLogTypeValid(type))
    return;

  m_types[type].throws = shouldThrow;
}

bool Logger::ThrowOnLogTypeEnabled(Log::Type type)
{
  LogAutoLock lock(&m_settingsLock, LogAutoLock::Read);

  if (!isLogTypeValid(type))
    return false;

  return m_types[type].throws;
}

/**
 * Maps type to level. Does not check ranges.
 *
 * @param type
 * @param level
 *
 * @return bool
 */
bool Logger::isTypeInLevel(Log::Type type, Log::Level level)
{
  return m_levelsMap[level].types[type];
}
