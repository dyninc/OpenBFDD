/**************************************************************
* Copyright (c) 2010-2014, Dynamic Network Services, Inc.
* Author - Jake Montgomery (jmontgomery@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once
#include <string>
#include <stdio.h>
#include <cstdarg>
#include "LogTypes.h"

/**
 * Provides common logging functionality, without specific knowledge of the
 * log levels and types used by the application.
 *
 * This should be subclasses to provide the actual log levels and types. Note
 * that, for performance reasons, most of the methods are not virtual.
 *
 * Note that the organization is intended to separate the Logger from the Log
 * types and levels only to make upstreaming easier, while maintaining strict
 * type safety for the Log::Level and Log::Type enums.
 *
 * It is thread safe, as long as only one is created. Only create one subclass
 * per process.
 */
class Logger
{
public:
  /**
   * Extended time info
   *
   */
  struct TimeInfo
  {
    enum Type
    {
      None,
      Real,
      Mono
    };
  };

  static const size_t MaxMessageLen  = 1024;

  /**
   *  Create a logger. Uses stderr for logging by default.
   *  May throw on memory error.
   */
  Logger();

  virtual ~Logger();

  /**
   * All logging goes to syslog. Stops file logging, if any.
   * Note that teeLogToStderr uses the built in syslog facility, and is separate
   * from the SetStdErr().
   * Logging due to SetStdOut() and  SetStdErr() continues.
   *
   * @param ident [in] - The name to use for syslogging.
   * @param teeLogToStderr [in] - Should all logging also be sent to stdout?
   */
  void LogToSyslog(const char *ident, bool teeLogToStderr);


  /**
   * All logging goes to the file. Stops syslog logging.
   *
   * @param logFilePath [in] - The path of the file to log to.
   *
   * @return bool - false if failed to open file.
   */
  bool LogToFile(const char *logFilePath);

  /**
   * Sets the level for Optional() logging.
   *
   * @param logLevel [in] - The new level.
   */
  //void SetLogLevel(Log::Level logLevel);

  /**
   * Enables or disables one specific type of logging.
   *
   * @param type
   * @param enable
   */
  void EnableLogType(Log::Type type, bool enable);

  /**
   * Checks if the given log type is currently enabled
   *
   * @param level
   *
   * @return bool
   */
  bool LogTypeEnabled(Log::Type type);

  /**
   *
   * Enables/disables throwing a LogException() for the given log type.
   *
   *
   * @param type
   * @param shouldThrow
   */
  void ThrowOnLogType(Log::Type type, bool shouldThrow);

  /**
   * Checks if the given log type will throw an exception.
   *
   * @param type
   *
   * @return bool
   */
  bool ThrowOnLogTypeEnabled(Log::Type type);

  /**
   * Gets the name of the given log type.
   *
   * @param type
   *
   * @return const char* - "unknown" on failure. Never null.
   */
  const char* LogTypeToString(Log::Type type);

  /**
   * Gets the description of the given log type.
   *
   * @param type
   *
   * @return const char* - "" if there is no description. Never null.
   */
  const char* LogTypeDescription(Log::Type type);

  /**
   * Gets the log type for the string.
   *
   * @param str
   *
   * @return Logger::Type - TypeCount on failure.
   */
  Log::Type StringToLogType(const char *str);


  /**
   * Resets all log typed based on the given level
   *
   * @param level
   */
  void SetLogLevel(Log::Level level);


  /**
   * Sets the given type to log to stdout in addition to normal logging. The
   * message will be logged to stdout only if the log type is also enabled.
   *
   * @param type
   * @param shouldOut
   */
  void SetStdOut(Log::Type type, bool shouldOut);



  /**
   * Sets the all types in the level to log to stdout in addition to normal
   * logging. The message will be logged to stdout only if the log type is also
   * enabled.
   *
   * @param type
   * @param shouldOut
   */
  void SetStdOut(Log::Level level, bool shouldOut);

  /**
   * Sets the given type to log to stderr in addition to normal logging. The
   * message will be logged to stdout only if the log type is also enabled.
   *
   * @param type
   * @param shouldOut
   */
  void SetStdErr(Log::Type type, bool shouldOut);

  /**
   * Sets the all types in the level to log to stdout in addition to normal
   * logging. The message will be logged to stdout only if the log type is also
   * enabled.
   *
   * @param type
   * @param shouldOut
   */
  void SetStdErr(Log::Level level, bool shouldOut);

  /**
     * Configure extended time info.
   *
     * @param type
   */
  void SetExtendedTimeInfo(TimeInfo::Type type);

  /**
   * Turns on or off adding log type name to logging string.
   *
   * @param printTypeNames
   */
  void SetPrintTypeNames(bool printTypeNames);


  /**
   * Gets the name of the level.
   *
   * @param level
   *
   * @return const char* - "unknown" on failure. Never null.
   */
  const char* LogLevelToString(Log::Level level);


  /**
   * Gets the log level for the given string
   *
   * @param str
   *
   * @return Log::Level - Returns Log::LevelCount on failure
   */
  Log::Level StringToLogLevel(const char *str);



  /**
   * Optionally logs a message depending on the setting for that type. No newline
   * is needed.
   *
   * @param type
   * @param format
   */
  void Optional(Log::Type type, const char *format, ...) ATTR_FORMAT(printf, 3, 4);


  /**
   * Same as Optional(type, format, ...)
   *
   * @param type
   * @param args
   */
  void OptionalVa(Log::Type type, const char* format, va_list args);


  /**
   * Always logs message.
   * No newline is needed.
   */
  void Message(Log::Type type, const char* format, ...)ATTR_FORMAT(printf, 3, 4);
  void MessageVa(Log::Type type, const char *format, va_list args);

  /**
   * shortcut for Message(Logger::Error,
   */
  void LogError(const char* format, ...)ATTR_FORMAT(printf, 2, 3);

  /**
   * shortcut for Message(Logger::Warn,
   */
  void LogWarn(const char* format, ...)ATTR_FORMAT(printf, 2, 3);

  /**
   * Logs the message, then the errno string for the error. Always logs as Error.
   *
   * @param errnum
   * @param mgs
   */
  void ErrnoError(int errnum, const char* mgs);

  /**
   * Logs a message and exits the program.
   * If using syslog then this uses the LOG_CRIT level.
   * No newline is needed.
   */
  void Fatal(const char* format, ...)ATTR_FORMAT(printf, 2, 3);

protected:

  struct TypeInfo
  {
    bool enabled;
    bool throws;
    int syslogPriority;
    const char *logName;  // for use in log file.
    const char *name; // name to use when enabling/disabling.
    const char *description; // Optional description of log type
    bool toStdErr;
    bool toStdOut;
  };

  struct LevelInfo
  {
    LevelInfo() : name(), types() { }
    std::string name;
    bool *types;    // size is m_typeCount
  };

  // m_types is protected by m_settingsLock, and should only be modified by the
  // derived class during construction.
  TypeInfo *m_types; // The type info for each type
  const size_t m_typeCount;   // The size of m_types. Not locked.

  // This will never change after construction
  LevelInfo *m_levelsMap;
  const size_t m_levelCount;  // The size of m_levelsMap. Not locked.

  // Copies m_levelsMap types from source to dest
  void copyLevelTypes(Log::Level source, Log::Level dest);
  // Sets all types in the log level to value
  void setLevelTypes(Log::Level level, bool value);

private:

  void closeSyslog();
  void closeLogFile();
  bool isTypeInLevel(Log::Type type, Log::Level level);
  void logMsg(const TypeInfo *typeInfo, const char *format, va_list args);
  bool isLogTypeValid(Log::Type type);
  bool isLogLevelValid(Log::Level level);

  //
  // All these are protected by m_settingsLock
  //
  pthread_rwlock_t m_settingsLock;
  FILE *m_logFile;  // File to log to (if any)
  std::string m_logFilePath; // The path of the current m_logFile.
  bool m_useSyslog; // Use syslog
  std::string m_ident; // Used with syslog
  TimeInfo::Type m_extendedTimeInfo;
  bool m_printTypeNames; // Add the type names to the log

};
