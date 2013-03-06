/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Logging routines
#pragma once
#include <string>
#include <stdio.h>
#include <cstdarg>

namespace openbfdd
{
  /**
   * Logging class. Is thread safe.
   * Only create one per process.
   */
  class Log
  {
  public:
    enum Level
    {
      None = 0,
      Minimal,
      Normal,
      Detailed,
      Dev,  // This will change with the developers whim.
      All,  // be careful .. this is a lot of info.

      LevelCount // used only to signify error
    };

    enum Type
    {
      Critical = 0, // Serious failure.
      Error, // Major failure
      Warn, // Problematic condition
      App, // General important info about app
      AppDetail, // Detailed info about app.
      Session, // Session creation and state change.
      SessionDetail, // Session creation and state change.
      Discard, // Packet discards and errors
      DiscardDetail, // Contents of (some) discarded packets.
      Packet, // Detailed packet info
      PacketContents, // Log every non-discarded packet.
      Command, // Commands
      CommandDetail, // Detailed info about command processing
      TimerDetail, // Detailed info about timers and scheduler.
      Temp,  // Special temporary log messages

      TypeCount
    };

    static const size_t MaxMessageLen;

    /**
     *  Create a logger. Uses stderr for logging by default.
     */
    Log();

    ~Log();

    /**
     * All logging goes to syslog. Stops file logging, if any.
     *
     * @param ident [in] - The name to use for syslogging.
     * @param teeLogToStderr [in] - Should logging also be sent to stdout?
     */
    void LogToSyslog(const char *ident, bool teeLogToStderr);


    /**
     * All logging goes to the file. Stops syslog logging.
     *
     * @param logFilePath [in] - The path of the file to log to. NULL for stderr.
     *
     * @return bool - false if failed to open file.
     */
    bool LogToFile(const char *logFilePath);

    /**
     * Enables or disables one specific type of logging for Optional() messages.
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
     * Gets the name of the given log type.
     *
     * @param type
     *
     * @return const char* - "unknown" on failure. Never null.
     */
    const char* LogTypeToString(Log::Type type);

    /**
     * Gets the log type with the given name.
     *
     * @param str [in] - the name to find.
     *
     * @return Log::Type - TypeCount on failure.
     */
    Log::Type StringToLogType(const char *str);


    /**
     * Sets enabled/disabled for all log types based on the given level.
     *
     * @param level [in] - The new level.
     */
    void SetLogLevel(Log::Level level);

    /**
     * Turns on or off extended time info.
     *
     * @param useExtendedTime
     */
    void SetExtendedTimeInfo(bool useExtendedTime);

    /**
     * Gets the name of the level.
     *
     * @param level
     *
     * @return const char* - "unknown" on failure. Never null.
     */
    static const char* LogLevelToString(Log::Level level);


    /**
     * Gets the log level for the given string
     *
     * @param str
     *
     * @return Log::Level - Returns Log::LevelCount on failure
     */
    static Log::Level StringToLogLevel(const char *str);



    /**
     * Optionally logs a message depending on the setting for that type. No newline
     * is needed.
     *
     * @param type
     * @param format
     */
    void Optional(Log::Type type, const char *format, ...) ATTR_FORMAT(printf, 3, 4);



    /**
     * Always logs message.
     * No newline is needed.
     */
    void Message(Log::Type type, const char* format, ...)ATTR_FORMAT(printf, 3, 4);
    void MessageVa(Log::Type type, const char *format, va_list args);

    /**
     * shortcut for Message(Log::Error,
     */
    void LogError(const char* format, ...)ATTR_FORMAT(printf, 2, 3);


    /**
     * shortcut for Message(Log::Warn,
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

  private:

    void closeSyslog();
    void closeLogFile();
    void logMsg(int syslogPriority, const char *type, const char *format, va_list args);

    //
    // All these are protected by m_settingsLock
    //
    pthread_rwlock_t m_settingsLock;
    FILE *m_logFile;  // File to log to (if any)
    std::string m_logFilePath; // The path of the current m_logFile.
    bool m_useSyslog; // Use syslog
    std::string m_ident; // Used with syslog
    bool m_extendedTimeInfo; // Include full timestamp on every message.
    struct TypeInfo
    {
      bool enabled;
      int syslogPriority;
      const char *logName;  // for use in log file.
      const char *name; // name to use when enabling/disabling.
    };
    TypeInfo m_types[Log::TypeCount];
  };
}
