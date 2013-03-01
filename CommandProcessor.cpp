/**************************************************************
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "CommandProcessor.h"
#include "utils.h"
#include "Beacon.h"
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <stdarg.h>

using namespace std;

namespace openbfdd
{

  class CommandProcessorImp : public CommandProcessor
  {

  protected:
    Beacon *m_beacon; // never null, never changes

    //
    // These are only accessed from thread.
    //
    Socket m_listenSocket;
    Socket m_replySocket;
    RecvMsg m_inCommand;
    vector<char> m_inReplyBuffer;  // only use  messageReply and friends.
    string m_inCommandLogStr;


    //
    // These are protected by m_mainLock
    //
    QuickLock m_mainLock;
    uint16_t m_port; /// port to listen on.
    pthread_t m_listenThread;
    volatile bool m_isThreadRunning;
    volatile bool m_threadInitComplete; // Set to true after  m_isThreadRunning set true the first time
    volatile bool m_threadStartupSuccess;   //only valid after m_isThreadRunning has been set to true.
    volatile bool m_stopListeningRequested;
    WaitCondition m_threadStartCondition;


  public:
    CommandProcessorImp(Beacon &beacon) :  CommandProcessor(beacon),
       m_beacon(&beacon),
       m_listenSocket(),
       m_replySocket(),
       m_inCommand(MaxCommandSize, 0),
       m_inReplyBuffer(MaxReplyLineSize + 1),
       m_mainLock(true),
       m_isThreadRunning(false),
       m_threadInitComplete(false),
       m_threadStartupSuccess(true),
       m_stopListeningRequested(false)
    {
      m_inCommandLogStr.reserve(MaxCommandSize);  // could end up needing more, but this is a good start.
    }

    virtual ~CommandProcessorImp()
    {
      StopListening();
    }

    /**
     * See CommandProcessor::BeginListening().
     */
    virtual bool BeginListening(uint16_t port)
    {
      AutoQuickLock lock(m_mainLock, true);

      pthread_attr_t attr;

      if (m_isThreadRunning)
      {
        LogVerifyFalse("Command Processer already running.");
        return true;
      }

      if (pthread_attr_init(&attr))
        return false;
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);  // we will handle synchronizing

      m_port = port;
      m_isThreadRunning = false;
      m_threadInitComplete = false;
      m_threadStartupSuccess = true;
      m_stopListeningRequested = false;

      if (pthread_create(&m_listenThread, NULL, doListenThreadCallback, this))
        return false;

      // Wait for listening, or error.
      while (true)
      {
        lock.LockWait(m_threadStartCondition);

        if (!m_threadInitComplete)
          continue; // spurious signal.

        // We can now allow the worker thread to shutdown if it wants to.
        if (!m_threadStartupSuccess)
        {
          lock.UnLock();
          StopListening();  // Ensure that thread is finished before we return...in case we try again immediately.
          return false;
        }

        break;
      }

      return true;
    }

    /**
     * See CommandProcessor::StopListening().
     */
    virtual void StopListening()
    {
      AutoQuickLock lock(m_mainLock, true);

      if (!m_isThreadRunning)
        return;

      m_stopListeningRequested = true;

      // We need to wait for it to
      while (m_isThreadRunning)
        lock.LockWait(m_threadStartCondition);
    }

  protected:

    static void* doListenThreadCallback(void *arg)
    {
      reinterpret_cast<CommandProcessorImp *>(arg)->doListenThread();
      return NULL;
    }

    void doListenThread()
    {
      bool initSuccess;
      AutoQuickLock lock(m_mainLock, true);

      gLog.Optional(Log::AppDetail, "Listen Thread Started");

      initSuccess = initListening();
      m_threadStartupSuccess = initSuccess;
      m_isThreadRunning = true;
      m_threadInitComplete = true;

      // Signal setup completed (success, or failure).
      lock.SignalAndUnlock(m_threadStartCondition);

      // do Stuff
      if (initSuccess)
      {
        while (processMessage())
        {}
      }

      lock.Lock();
      m_isThreadRunning = false;
      lock.SignalAndUnlock(m_threadStartCondition);
      gLog.Optional(Log::AppDetail, "Listen Thread Shutdown");
      return;
    }


    /**
     *
     * Call only from listen thread.
     * Call with  m_mainLock held.
     *
     * @return bool - false if listening setup failed.
     */
    bool initListening()
    {
      // Do this so low memory will not cause distorted messages
      if (!UtilsInitThread())
      {
        gLog.Message(Log::Error,  "Failed to initialize listen thread. TLS memory failure.");
        return false;
      }

      m_listenSocket.SetLogName(FormatShortStr("Control listen socket on port %"PRIu16, m_port));

      if (!m_listenSocket.OpenTCP(Addr::IPv4))
        return false;

      if (!m_listenSocket.SetBlocking(false))
        return false;

      if (!m_listenSocket.SetReusePort(true))
        return false;

      // TODO: listen address should be settable.
      SockAddr addr("127.0.0.1", m_port);

      if (!m_listenSocket.Bind(addr))
        return false;

      if (!m_listenSocket.Listen(3))
        return false;

      return true;
    }

    struct Result
    {
      enum Type
      {
        Success,
        Timeout,
        Error,
        StopListening   // requested shutdown
      };
    };

    /**
     * Waits for the socket to have read data.
     *
     * @param fd
     * @param pollTimeInMs - poll time for m_stopListeningRequested in milliseconds.
     * @param maxWaitInMs - The maximum time to wait in milliseconds. May be rounded
     *                    to the nearest pollTimeInMs.
     *
     * @return Result - Success, GaveUp, Error or StopListening
     */
    Result::Type waitForSocketRead(int fd, uint32_t pollTimeInMs, uint32_t maxWaitInMs = 0)
    {
      int result;
      int waits = 0;
      fd_set waitOn;
      TimeSpec maxTime;

      if (maxWaitInMs)
        maxTime = TimeSpec::MonoNow() + TimeSpec(TimeSpec::Millisec, maxWaitInMs);

      while (!isStopListeningRequested())
      {
        if (maxWaitInMs)
        {
          if (TimeSpec::MonoNow() > maxTime)
          {
            gLog.Optional(Log::Command, "Waiting timed out after %d polls.", waits);
            return Result::Timeout;
          }
        }

        struct timeval waitTime;

        // setup fd_set and wait time
        waitTime.tv_sec = pollTimeInMs / 1000;
        waitTime.tv_usec = (pollTimeInMs % 1000) * 1000;

        FD_ZERO(&waitOn);
        FD_SET(fd, &waitOn);
        result = select(fd + 1, &waitOn, NULL, NULL, &waitTime);
        if (result < 0)
        {
          if (errno != EINTR)
          {
            gLog.ErrnoError(errno,  "socket wait: ");
            return Result::Error;
          }
        }
        else if (result > 0)
        {
          LogAssert(result == 1);
          return Result::Success;
        }

        waits++;

        //gLog.Optional(Log::Debug, "Still waiting %d\n", waits);
      }
      return Result::StopListening;
    }

    /**
     * Helper for processMessage.
     *
     * @param me
     */
    static void closeSyncReplySocket(CommandProcessorImp *me)
    {
      if (me)
        me->m_replySocket.Close();
    }

    /**
     *
     * Process the next command.
     * This can return false because m_stopListeningRequested was set to true, or
     * because the "Stop" command was executed, or due to a fatal error that will
     * not allow listening to continue.
     *
     * Call only from listen thread.
     *
     * @throw - yes
     *
     * @return bool - false if listening should stop.
     */
    bool processMessage()
    {

      Result::Type waitResult;
      Socket connectedSocket;
      RiaaNullBase<CommandProcessorImp, closeSyncReplySocket> syncConnectedSocket(this);


      // Since we have a non-blocking socket, we must wait for a connection
      waitResult = waitForSocketRead(m_listenSocket, 320);
      if (waitResult != Result::Success)
        return false;

      // accept a connection
      if (!m_listenSocket.Accept(connectedSocket))
      {
        // We do not quit on error?
        return true;
      }
      connectedSocket.SetLogName(FormatShortStr("Command connection to %s",  connectedSocket.GetAddress().ToString()));
      m_replySocket = connectedSocket; // note connectedSocket still 'owns' the socket.
      m_replySocket.SetLogName(connectedSocket.LogName());

      // Got a connection for the command. Now wait for a command. Since we are
      // non-blocking, we use select again.
      waitResult = waitForSocketRead(connectedSocket, 200, 10000);
      if (waitResult != Result::Success)
        return true;  // failure here does not cause full shutdown.

      // Should be ready with a command
      while (true)
      {
        if (isStopListeningRequested())
          return false;

        if (!m_inCommand.DoRecv(connectedSocket, MSG_DONTWAIT))
        {
          // Errors
          if (m_inCommand.GetLastError() == EAGAIN)
            gLog.Optional(Log::Command, "Incomplete message ... waiting."); // Must not have the full message. Wait for it?
          else if (m_inCommand.GetLastError() == EINTR)
            gLog.Optional(Log::Command, "Interrupted message ... trying again.");
          else if (m_inCommand.GetLastError() == ECONNRESET)
          {
            gLog.Message(Log::Command, "Communication connection reset.");
            return true;
          }
        }
        else if (m_inCommand.GetDataSize() == 0)
        {
          gLog.LogError("Empty communication message.");
          return true;
        }
        else
        {
          // Got a message
          gLog.Optional(Log::Command, "Message size %zu.", m_inCommand.GetDataSize());
          try
          {
            dispatchMessage((char *)m_inCommand.GetData(),  m_inCommand.GetDataSize());
          }
          catch (std::exception &e)
          {
            messageReplyF("Unable to complete request. Exception: %s\n", e.what());
          }
          return true;
        }
      }
      return true;
    }

    /**
     * Checks the validity of the message, and handles it.
     *
     * Call only from listen thread.
     *
     * @param message
     * @param message_size
     *
     */
    void dispatchMessage(const char *message, size_t message_size)
    {
      const char *pos, *end;
      const char *messageEnd = message + message_size - 1;
      int paramCount = 0;

      if (message_size < sizeof(uint32_t))
      {
        gLog.Optional(Log::Command, "Communication message too short. Ignoring.");
        return;
      }

      if (ntohl(*(uint32_t *)message) != MagicMessageNumber)
      {
        gLog.Optional(Log::Command, "Message invalid. No magic number. Ignoring.");
        return;
      }

      pos = message + sizeof(uint32_t);

      // Verify the message
      bool log = gLog.LogTypeEnabled(Log::Command);
      m_inCommandLogStr.clear();
      while (true)
      {
        end = pos;
        for (; end <= messageEnd && *end != '\0'; end++)
        {}
        if (end > messageEnd)
        {
          gLog.Optional(Log::Command, "Message invalid. No terminator. Ignoring.");
          return;
        }
        if (end == pos)
        {
          if (pos !=  messageEnd)
          {
            gLog.Optional(Log::Command, "Message invalid. Terminator came before the end.");
            return;
          }
          break;
        }
        else
        {
          paramCount++;
          if (paramCount != 1)
            m_inCommandLogStr.push_back(' ');
          m_inCommandLogStr.append(pos);
        }

        pos = end + 1;
      }

      if (paramCount == 0)
      {
        gLog.Message(Log::Command, "Empty message received.");
        return;
      }

      if (log)
        gLog.Optional(Log::Command, "Message %d <%s>\n", paramCount, m_inCommandLogStr.c_str());



      // We have a valid message
      handleMessage(message + sizeof(uint32_t), paramCount);
    }

    /**
     * Handles a received message
     *
     * Call only from listen thread.
     *
     * @param replySocket
     * @param message - The message itself.
     * @param paramCount - The number of post message parameters.
     */
    void handleMessage(const char *message, int paramCount)
    {
      (void)paramCount;

      if (0 == strcasecmp(message, "stop"))
      {
        handle_Stop();
      }
      else if (0 == strcasecmp(message, "version"))
      {
        handle_Version();
      }
      else if (0 == strcasecmp(message, "connect"))
      {
        handle_Connect(message);
      }
      else if (0 == strcasecmp(message, "allow"))
      {
        handle_Allow(message);
      }
      else if (0 == strcasecmp(message, "block"))
      {
        handle_Block(message);
      }
      else if (0 == strcasecmp(message, "status"))
      {
        handle_Status(message);
      }
      else if (0 == strcasecmp(message, "log"))
      {
        handle_Log(message);
      }
      else if (0 == strcasecmp(message, "session"))
      {
        handle_Session(message);
      }
      else if (0 == strcasecmp(message, "test"))
      {
        handle_Test(message);
      }
      else
      {
        messageReplyF("Unknown command <%s>\n", message);
      }
    }

    typedef intptr_t(CommandProcessorImp::*BeaconCallback)(Beacon *beacon, void *userdata);

    struct BeaconCallbackData
    {
      CommandProcessorImp *me;
      void *userdata;
      BeaconCallback callback;
      bool wasShuttingDown;
      intptr_t result;
      bool exceptionThrown;
    };

    static void handleBeaconCallback(Beacon *beacon, void *userdata)
    {
      BeaconCallbackData *data = (BeaconCallbackData *)userdata;

      if (beacon->IsShutdownRequested())
      {
        data->wasShuttingDown = true;
        return;
      }

      try
      {
        data->result = (data->me->*(data->callback))(beacon, data->userdata);
      }
      catch (std::exception &e)  // catch all exceptions .. is this too broad?
      {
        data->exceptionThrown = true;
        gLog.Message(Log::Error, "Beacon callback failed due to exception: %s", e.what());
      }
    }

    /**
     * Queues a beacon callback. Does not return until operation is completed.
     * Will respond using messageReply if the operation can not start due to a
     * pending shutdown.
     *
     * @param userdata
     *
     * @return bool - false on failure to run the callback.
     */
    bool doBeaconOperation(BeaconCallback callback, void *userdata, intptr_t *result = NULL)
    {
      BeaconCallbackData data;
      data.me = this;
      data.userdata = userdata;
      data.callback = callback;
      data.wasShuttingDown = false;
      data.result = 0;
      data.exceptionThrown = false;

      if (!m_beacon->QueueOperation(handleBeaconCallback, &data, true /* waitForCompletion*/))
      {
        messageReply("Unable to complete request (beacon is shutting down or low memory).\n");
        return false;
      }

      if (data.exceptionThrown)
      {
        messageReply("Unable to complete request because an exception was thrown. Likely out of memory.\n");
        return false;
      }

      if (data.wasShuttingDown)
      {
        messageReply("Unable to complete request because the beacon is shutting down.\n");
        return false;
      }

      if (result)
        *result = data.result;

      return true;
    }


    /**
     * Holds enough info to locate a session, or marks for "All" sessions.
     *
     */
    struct SessionID
    {
      SessionID() : allSessions(false), whichId(0), whichRemoteAddr(), whichLocalAddr() { }
      void Clear() { allSessions = false; whichId = 0; whichRemoteAddr.clear(); whichLocalAddr.clear();}
      bool IsValid() const { return allSessions || whichId != 0 || HasIpAddresses();}
      bool HasIpAddresses() const { return  whichRemoteAddr.IsValid() && whichLocalAddr.IsValid();}
      void SetAddress(bool local, const IpAddr &addr) { if (local)
          whichLocalAddr = addr;
        else
          whichRemoteAddr = addr;}

      bool allSessions;
      uint32_t whichId;
      IpAddr whichRemoteAddr;
      IpAddr whichLocalAddr;
    };

    /**
     * Converts a set of parameters to a local/remote ip address pair.
     *
     * @param inOutParam [in/out] - The first parameter to examine. On success this
     *                   will point to the last parameter used. On failure it will
     *                   remain unchanged.
     * @param sessionId [out] - Cleared on input. On success will have ip address
     *                  (sessionId.HasIpAddresses() will be true).
     * @param errorMsg [out] - On error it will contain a message.
     *
     * @return bool - false if the string could not be converted.
     */
    bool paramToIpPair(const char **inOutParam, SessionID &sessionId, string &errorMsg)
    {
      const char *command = *inOutParam;
      const char *str = *inOutParam;
      bool local;
      SessionID temp;
      IpAddr addrVal;

      sessionId.Clear();

      if (0 == strcmp(str,  "remote"))
        local = false;
      else if (0 == strcmp(str,  "local"))
        local = true;
      else
      {
        errorMsg = FormatBigStr("Error: Unknown <%s> should be 'remote' or 'local'.", str);
        return false;
      }

      str = getNextParam(str);
      if (!str)
      {
        errorMsg = FormatBigStr("Error: '%s' should be followed by an Pv4 or IPv6 address.", command);
        return false;
      }

      if (!addrVal.FromString(str))
      {
        errorMsg = FormatBigStr("Error: <%s> is not an IPv4 or IPv6 address.", str);
        return false;
      }

      temp.SetAddress(local, addrVal);

      str = getNextParam(str);
      if (!str)
      {
        errorMsg = FormatBigStr("Error: '%s' not found.", local ? "remote" : "local");
        return false;
      }

      local = !local;
      if (0 != strcmp(str,  local ? "local" : "remote"))
      {
        errorMsg = FormatBigStr("Error: unknown <%s>. '%s' ip must be followed by '%s'.", str, command, local ? "local" : "remote");
        return false;
      }

      command = str;
      str = getNextParam(str);
      if (!str)
      {
        errorMsg = FormatBigStr("Error: '%s' should be followed by an ip address.", command);
        return false;
      }

      if (!addrVal.FromString(str))
      {
        errorMsg = FormatBigStr("Error: <%s> is not an IPv4 or IPv6 address.", str);
        return false;
      }

      temp.SetAddress(local, addrVal);

      if (temp.whichLocalAddr.Type() != temp.whichRemoteAddr.Type())
      {
        errorMsg = FormatBigStr("Error: can not mix IPv4 and IPv6 addresses.");
        return false;
      }

      sessionId = temp;
      *inOutParam = str;
      return true;
    }

    /**
     *
     * Converts a parameter (or set of parameters) to an id or ip address, or "all".
     * On failure sessionId is cleared.
     *
     * @param inOutParam [in/out] - The first parameters to examine. On parameters this
     *                   will point to the last parameter used. On failure it will
     *                   remain unchanged.
     * @param sessionId [out]
     * @param errorMsg [out] - On error it will contain a message.
     *
     * @return bool - false if the string could not be converted.
     */
    bool paramToIdOrIp(const char **inOutParam, SessionID &sessionId, string &errorMsg)
    {
      int64_t val;
      const char *str = *inOutParam;

      sessionId.Clear();

      if (0 == strcmp(str,  "all"))
      {
        sessionId.allSessions = true;
        return true;
      }

      if (0 == strcmp(str,  "remote") || 0 == strcmp(str,  "local"))
        return paramToIpPair(inOutParam, sessionId, errorMsg);

      // must be an id
      if (StringToInt(str, val) && val != 0)
      {
        sessionId.whichId = (uint32_t)val;
        return true;
      }

      errorMsg = FormatBigStr("Unknown <%s>.", str);

      return false;
    }


    /**
     *
     * Call only from callback thread.
     * Finds the session for the id or ip.
     * Only one will be used.
     *
     * @param beacon
     * @param sessionId [in] - The session to find. Fails if allSessions.
     *
     * @return Session* - NULL if there is no such session. Or sessionId is "all"
     */
    Session* findSession(Beacon *beacon, const SessionID &sessionId)
    {
      if (!beacon)
        return NULL;

      if (!sessionId.IsValid() || sessionId.allSessions)
        return NULL;

      if (sessionId.whichId != 0)
        return beacon->FindSessionId(sessionId.whichId);
      if (sessionId.HasIpAddresses())
        return beacon->FindSessionIp(sessionId.whichRemoteAddr, sessionId.whichLocalAddr);

      return false;
    }


    /**
     *
     * Clears and fill the vector with all the sessions as described by sessionId.
     *
     * @param outList
     *
     * @return - false if sessionId is invalid, or does not represent an live
     *         session. true if there are no sessions, but sessionId is all. true
     *         if sessions found
     */
    bool findSessionIdList(Beacon *beacon, const SessionID &sessionId, std::vector<uint32_t> &outList)
    {

      if (!sessionId.IsValid())
        return false;

      if (sessionId.allSessions)
      {
        beacon->GetSessionIdList(outList);
        return true;
      }
      outList.clear();

      Session *session = findSession(beacon, sessionId);
      if (!session)
        return false;

      outList.push_back(session->GetId());
      return true;
    }


    /**
     * Sends reply message complaining that the given session could not be located.
     *
     * @param sessionId [in] - The session to find. Fails if allSessions.
     */
    void reportNoSuchSession(const SessionID &sessionId)
    {
      if (sessionId.whichId != 0)
        messageReplyF("No session with id=%u.\n", sessionId.whichId);
      else if (sessionId.HasIpAddresses())
        messageReplyF("No session with local ip=%s and remote ip=%s.\n", sessionId.whichLocalAddr.ToString(), sessionId.whichRemoteAddr.ToString());
      else
        messageReply("Unknown session specifier.\n");
    }


    void handle_Stop()
    {
      m_beacon->RequestShutdown();
      messageReply("stopping\n");
    }

    void handle_Version()
    {
      messageReplyF("%s v%s\n", BeaconAppName, SofwareVesrion);
    }

    /**
     * "log" command.
     * Format 'log' 'level' name - to set logging level
     * Format 'log' type ['yes'|'no'] - enable/disable specific logging.
     *
     */
    void handle_Log(const char *message)
    {
      const char *itemString;
      static const char *itemValues = "'level', 'type' or 'timing'";

      itemString = getNextParam(message);
      if (!itemString)
      {
        messageReplyF("Must specify: %s.\n", itemValues);
        return;
      }

      if (0 == strcmp(itemString, "level"))
      {
        Log::Level level;
        const char *levelString = getNextParam(itemString);
        if (!levelString)
        {
          messageReply("Must specify a level name or 'list'.\n");
          return;
        }

        if (0 == strcmp("list", levelString))
        {
          string str;

          str.reserve(Log::LevelCount * 10);

          for (int index = 0; index < Log::LevelCount; index++)
          {
            if (!str.empty())
              str += ", ";
            str += Log::LogLevelToString(Log::Level(index));
          }
          messageReplyF("Available log levels: %s\n",  str.c_str());
          return;
        }

        level = Log::StringToLogLevel(levelString);
        if (level == Log::LevelCount)
        {
          messageReplyF("Unknown level: %s.\n", levelString);
          return;
        }


        gLog.SetLogLevel(level);
        messageReplyF("Log level set to %s.\n", levelString);
        return;
      }
      else if (0 == strcmp(itemString, "type"))
      {
        Log::Type type;
        bool enable, wasEnabled;
        const char *paramString = getNextParam(itemString);
        if (!paramString)
        {
          messageReply("'type' must be followed by 'list' or a log type.\n");
          return;
        }

        if (0 == strcmp("list", paramString))
        {
          string str;
          str.reserve(Log::TypeCount * 10);

          for (int index = 0; index < Log::TypeCount; index++)
          {
            if (!str.empty())
              str += ", ";
            str += gLog.LogTypeToString(Log::Type(index));
          }
          messageReplyF("Available log types: %s\n",  str.c_str());
          return;
        }

        type = gLog.StringToLogType(paramString);
        if (type == Log::TypeCount)
        {
          messageReplyF("Unknown log type: %s.\n", paramString);
          return;
        }

        const char *actionString = getNextParam(paramString);
        if (!actionString)
        {
          messageReply("Must specify 'yes' or 'no'.\n");
          return;
        }

        if (0 == strcmp("yes", actionString))
          enable = true;
        else if (0 == strcmp("no", actionString))
          enable = false;
        else
        {
          messageReply("Must specify 'yes' or 'no'.\n");
          return;
        }

        wasEnabled = gLog.LogTypeEnabled(type);
        gLog.EnableLogType(type, enable);

        messageReplyF("Log type %s set to %s, was %s\n", paramString, enable ? "yes" : "no", wasEnabled ? "yes" : "no");
        return;
      }
      else if (0 == strcmp(itemString, "timing"))
      {
        bool enable;

        const char *actionString = getNextParam(itemString);
        if (!actionString)
        {
          messageReply("Must specify 'yes' or 'no'.\n");
          return;
        }

        if (0 == strcmp("yes", actionString))
          enable = true;
        else if (0 == strcmp("no", actionString))
          enable = false;
        else
        {
          messageReply("Must specify 'yes' or 'no'.\n");
          return;
        }

        gLog.SetExtendedTimeInfo(enable);
        messageReplyF("Extended time logging %s.\n", enable ? "enabled" : "disabled");
        return;
      }
      else
      {
        messageReplyF("'log' must be followed by one of %s. Unknown <%s>\n", itemValues, itemString);
      }
    }

    /**
     * "connect" command.
     * Format 'connect' ip.
     * Starts an 'active' session with the given ip.
     */
    void handle_Connect(const char *message)
    {
      SessionID address;
      const char *addressString;
      intptr_t result;
      string error;

      addressString = getNextParam(message);
      if (!addressString)
      {
        messageReply("Must supply 'local ip remote ip' address pair.\n");
        return;
      }

      if (!paramToIpPair(&addressString, address, error))
      {
        messageReplyF("'connect' must be followed by an ip pair. %s\n", error.c_str());
        return;
      }

      if (doBeaconOperation(&CommandProcessorImp::doHandleConnect, &address, &result))
      {
        if (result)
          messageReplyF("Opened connection from local %s to remote %s\n", address.whichLocalAddr.ToString(), address.whichRemoteAddr.ToString());
        else
          messageReplyF("Failed to open connection from local %s to remote %s\n", address.whichLocalAddr.ToString(), address.whichRemoteAddr.ToString());
      }
    }

    intptr_t doHandleConnect(Beacon *beacon, void *userdata)
    {
      SessionID *addr = reinterpret_cast<SessionID *>(userdata);

      if (!LogVerify(addr->HasIpAddresses()))
        return 0;

      return beacon->StartActiveSession(addr->whichRemoteAddr, addr->whichLocalAddr);
    }

    /**
     * "allow" command.
     * Format 'allow' ip
     */
    void handle_Allow(const char *message)
    {
      IpAddr address;
      const char *addressString;

      addressString = getNextParam(message);
      if (!addressString)
      {
        messageReply("Must supply ip address.\n");
        return;
      }

      if (!address.FromString(addressString))
      {
        messageReplyF("Invalid IPv4 or IPv6 address <%s>.\n", addressString);
        return;
      }

      if (doBeaconOperation(&CommandProcessorImp::doHandleAllow, &address))
        messageReplyF("Allowing connections from %s\n", address.ToString());
    }

    intptr_t doHandleAllow(Beacon *beacon, void *userdata)
    {
      IpAddr *addr = reinterpret_cast<IpAddr *>(userdata);
      beacon->AllowPassiveIP(*addr);
      return 0;
    }


    /**
     * "block" command.
     * Format 'block' ip
     */
    void handle_Block(const char *message)
    {
      IpAddr address;
      const char *addressString;

      addressString = getNextParam(message);
      if (!addressString)
      {
        messageReply("Must supply an IPv4 or IPv6 address.\n");
        return;
      }

      if (!address.FromString(addressString))
      {
        messageReplyF("Invalid IPv4 or IPv6 address <%s>.\n", addressString);
        return;
      }

      if (doBeaconOperation(&CommandProcessorImp::doHandleBlock, &address))
        messageReplyF("Blocking connections from %s. This will not terminate any ongoing session.\n", address.ToString());
    }

    intptr_t doHandleBlock(Beacon *beacon, void *userdata)
    {
      IpAddr *addr = reinterpret_cast<IpAddr *>(userdata);
      beacon->BlockPassiveIP(*addr);
      return 0;
    }

    struct StatusInfo
    {
      uint32_t id;
      uint32_t localDisc;
      uint32_t remoteDisc;
      IpAddr remoteAddress;
      IpAddr localAddress;
      bool isActiveSession; //active or passive role.
      Session::ExtendedStateInfo extState;
    };

    void fillSessionInfo(Session *session, StatusInfo &outInfo, int level)
    {
      outInfo.id = session->GetId();
      outInfo.remoteAddress =  session->GetRemoteAddress();
      outInfo.localAddress = session->GetLocalAddress();
      if (level >= 1)
      {
        outInfo.isActiveSession = session->IsActiveSession();
        outInfo.localDisc = session->GetLocalDiscriminator();
        outInfo.remoteDisc = session->GetRemoteDiscriminator();
      }

      if (level == 0)
        outInfo.extState.localState = session->GetState();
      else
        session->GetExtendedState(outInfo.extState);
    }


    /**
     * prints the stats info for a session.
     *
     * @param info
     * @param level
     * @param brief [in]- use codes instead of descriptive text.
     * @param compact [in]- true to print all info on one line.
     */
    void printStatusInfo(StatusInfo &info, int level, bool brief, bool compact)
    {
      const char *sep = compact ? "" : "\n ";
      bool useCommas = !brief;

      if (level < 1)
      {
        // Always brief and compact
        messageReplyF(" id=%u %slocal=%s %sremote=%s %sstate=%s\n",
                      info.id,
                      sep,
                      info.localAddress.ToString(),
                      sep,
                      info.remoteAddress.ToString(),
                      sep,
                      bfd::StateName(info.extState.localState));
      }
      else if (level == 1)
      {
        messageReplyF(" id=%u %slocal=%s %s %sremote=%s %sstate=%s%s %s\n",
                      info.id,
                      sep,
                      info.localAddress.ToString(),
                      info.isActiveSession ? "(a)" : "(p)",
                      sep,
                      info.remoteAddress.ToString(),
                      sep,
                      bfd::StateName(info.extState.localState),
                      info.extState.isHoldingState ? "<Forced>" : "",
                      info.extState.isSuspended ? "<Suspended>" : ""
                      );
      }
      else if (level >= 2)
      {

        messageReplyF(" id=%u %slocal=%s %s %sremote=%s %sLocalState=%s<%s%s%s> %sRemoteState=%s<%s> %sLocalId=%u %sRemoteId=%u %s",
                      info.id,
                      sep,
                      info.localAddress.ToString(),
                      info.isActiveSession ? (brief ? "(a)" : "(active)") : (brief ? "(p)" : "(passive)"),
                      sep,
                      info.remoteAddress.ToString(),
                      sep,
                      bfd::StateName(info.extState.localState),
                      info.extState.isHoldingState ? "Forced: " : "",
                      info.extState.isSuspended ? "Suspended: " : "",
                      brief ? ByteToString((uint8_t)info.extState.localDiag) : bfd::DiagString(info.extState.localDiag),
                      sep,
                      bfd::StateName(info.extState.remoteState),
                      brief ? ByteToString((uint8_t)info.extState.remoteDiag) : bfd::DiagString(info.extState.remoteDiag),
                      sep,
                      info.localDisc,
                      sep,
                      info.remoteDisc,
                      level > 2 ? sep : "\n"
                      );
      }

      if (level >= 3)
      {
        // Already added all level 2 stuff
        messageReplyF("Time=%s %sCurrentTxInterval=%s us %sCurrentRxTimeout=%s us %s",
                      makeTimeString(level, info.extState.uptimeList).c_str(),
                      sep,
                      FormatInteger(info.extState.transmitInterval, useCommas),
                      sep,
                      FormatInteger(info.extState.detectionTime, useCommas),
                      level > 2 ? sep : "\n"
                      );
      }

      if (level >= 4)
      {
        // Already added all level 3 stuff
        messageReplyF("LocalDetectMulti=%hhu "
                      "%sLocalDesiredMinTx=%s us %s%s"
                      "%sLocalRequiredMinRx=%s us %s"
                      "%sRemoteDetectMulti=%hhu "
                      "%sRemoteDesiredMinTx%s us "
                      "%sRemoteRequiredMinRx=%s us "
                      "%s",
                      info.extState.detectMult,
                      sep,
                      FormatInteger(info.extState.useDesiredMinTxInterval, useCommas),
                      (info.extState.desiredMinTxInterval == info.extState.useDesiredMinTxInterval)
                      ? "" : FormatShortStr("(pending %s us) ", FormatInteger(info.extState.desiredMinTxInterval, useCommas)),
                      (info.extState.desiredMinTxInterval == info.extState.defaultDesiredMinTxInterval)
                      ? "" : FormatShortStr("(def %s us) ", FormatInteger(info.extState.defaultDesiredMinTxInterval, useCommas)),
                      sep,
                      FormatInteger(info.extState.useRequiredMinRxInterval, useCommas),
                      (info.extState.requiredMinRxInterval == info.extState.useRequiredMinRxInterval)
                      ? "" : FormatShortStr("(pending %s us) ", FormatInteger(info.extState.requiredMinRxInterval, useCommas)),
                      sep,
                      info.extState.remoteDetectMult,
                      sep,
                      FormatInteger(info.extState.remoteDesiredMinTxInterval, useCommas),
                      sep,
                      FormatInteger(info.extState.remoteMinRxInterval, useCommas),
                      "\n"
                      );
      }
    }


    /**
     * Helper for printStatusInfo.
     *
     * @param level
     * @param uptimeList
     *
     * @return const char*
     */
    string makeTimeString(int level, list<Session::UptimeInfo> &uptimeList)
    {
      string str;

      if (!LogVerify(uptimeList.size() != 0))
        return str;

      str.reserve(35 * (level > 3 ? uptimeList.size() : 1));

      // Always add most recent time.
      addTimeString(str, uptimeList.front());

      if (level <= 3 || uptimeList.size() < 2)
        return str;

      list<Session::UptimeInfo>::iterator it = uptimeList.begin();
      for (it++; it != uptimeList.end(); it++)
        addTimeString(str, *it);
      return str;
    }

    /**
     *  Helper for makeTimeString. Adds single time info.
     *
     * @param level
     * @param outStr [in/out] - adjusted to point to null terminator.
     * @param outStrSize [in/out] - adjusted to reflect remaining buffer.
     * @param uptimeList
     *
     * @return bool - false if out of space. Always null terminates.
     */
    static void addTimeString(string &strout, Session::UptimeInfo &uptime)
    {
      TimeSpec elapsed;
      char buf[25];

      strout.append(bfd::StateName(uptime.state));
      if (uptime.forced)
        strout.append("/F(");
      else
        strout.append("(");

      elapsed = uptime.endTime - uptime.startTime;

      snprintf(buf, sizeof(buf), "%02u:%02u:%06.3f) ",
               uint32_t(elapsed.tv_sec / 3600),
               uint32_t(elapsed.tv_sec / 60),
               double(elapsed.tv_sec % 60) + double(elapsed.tv_nsec) / 1000000000L);

      strout.append(buf);
    }

    struct SingleStatusCallbackInfo
    {
      int level;
      SessionID sessionId;
      StatusInfo info;
    };


    /**
     * @return intptr_t - false if the session can not be located.
     */
    intptr_t doHandleSingleStatus(Beacon *beacon, void *userdata)
    {
      SingleStatusCallbackInfo *opInfo = reinterpret_cast<SingleStatusCallbackInfo *>(userdata);
      Session *session = findSession(beacon, opInfo->sessionId);
      if (!session)
        return 0;
      fillSessionInfo(session, opInfo->info, opInfo->level);
      return 1;
    }

    struct MultiStatusCallbackInfo
    {
      int level;
      vector<StatusInfo> infoList;
    };


    intptr_t doHandleMultiStatus(Beacon *beacon, void *userdata)
    {
      vector<uint32_t>::iterator idIt;
      vector<uint32_t> ids;
      MultiStatusCallbackInfo *opInfo = reinterpret_cast<MultiStatusCallbackInfo *>(userdata);
      vector<StatusInfo> *infoList = &opInfo->infoList;
      StatusInfo info;
      Session *session;

      beacon->GetSessionIdList(ids);
      infoList->clear();
      infoList->reserve(ids.size());
      for (idIt = ids.begin(); idIt != ids.end(); idIt++)
      {
        session = beacon->FindSessionId(*idIt);
        if (!session)
        {
          LogAssertFalse("No matching session for Id.");
          continue;
        }
        fillSessionInfo(session, info, opInfo->level);
        infoList->push_back(info);
      }
      return 0;
    }

    void handle_Status(const char *message)
    {
      const char *whichString, *nextString;
      int level = 1;
      bool brief = false;
      bool compact = false;
      SessionID sessionId;

      whichString = getNextParam(message);
      if (!whichString)
      {
        sessionId.allSessions = true;
        compact = true;
      }
      else
      {
        string error;

        if (!paramToIdOrIp(&whichString, sessionId, error))
        {
          messageReplyF("Must supply 'all', session id or 'remote ip local ip' before other settings. %s\n", error.c_str());
          return;
        }

        nextString = getNextParam(whichString);

        while (nextString)
        {
          if (0 == strcmp("brief",  nextString))
            brief = true;
          else if (0 == strcmp("compact",  nextString))
            compact = true;
          else if (0 == strcmp("nocompact",  nextString))
            compact = false;
          else if (0 == strcmp("level",  nextString))
          {
            int64_t val;
            nextString = getNextParam(nextString);
            if (!nextString)
            {
              messageReplyF("level must be followed by an integer.\n");
              return;
            }
            if (!StringToInt(nextString, val))
            {
              messageReplyF("level value must be an integer : <%s>.\n", nextString);
              return;
            }
            level = (int)val;
          }
          else
          {
            messageReplyF("Unrecognized  status setting <%s>.\n", nextString);
            return;
          }
          nextString = getNextParam(nextString);
        }
      }

      if (sessionId.allSessions)
      {
        MultiStatusCallbackInfo info;
        info.level = level;

        if (doBeaconOperation(&CommandProcessorImp::doHandleMultiStatus, &info))
        {
          vector<StatusInfo>::iterator it;
          messageReplyF("There are %zu sessions:\n", info.infoList.size());
          for (it = info.infoList.begin(); it != info.infoList.end(); it++)
          {
            if (!compact && it != info.infoList.begin())
            {
              messageReplyF("\nSession %u\n", it->id);
            }
            else
              messageReplyF("Session %u\n", it->id);
            printStatusInfo(*it, level, brief, compact);
          }
        }
      }
      else
      {
        intptr_t result;
        SingleStatusCallbackInfo info;
        info.level = level;
        info.sessionId = sessionId;

        if (doBeaconOperation(&CommandProcessorImp::doHandleSingleStatus, &info, &result))
        {
          if (result)
          {
            printStatusInfo(info.info, level, brief, compact);
          }
          else
          {
            reportNoSuchSession(info.sessionId);
            return;
          }
        }
      }
    }


    struct SessionCallbackInfo
    {
      enum Action
      {
        State,
        Kill,
        Reset,
        Suspend,
        Resume,
        SetMulti,
        SetMinTx,
        SetMinRx,
        SetCPI,
        SetAdminUpPoll
      };

      SessionID sessionId;
      bool defSetting; // if true then sessionId is not used
      Action action;
      bfd::State::Value state;
      uint32_t setValue;
    };

    /**
     *
     *
     * @param beacon
     * @param userdata
     *
     * @return intptr_t - false if the session can not be located.
     */
    intptr_t doHandleSession(Beacon *beacon, void *userdata)
    {
      vector<uint32_t> ids;
      vector<uint32_t>::iterator idIt;
      SessionCallbackInfo *info = reinterpret_cast<SessionCallbackInfo *>(userdata);

      if (info->defSetting)
      {
        // Default settings .. we do not need a session.
        if (info->action == SessionCallbackInfo::SetMulti)
          beacon->SetDefMulti(uint8_t(info->setValue));
        else if (info->action == SessionCallbackInfo::SetMinTx)
          beacon->SetDefMinTxInterval(info->setValue);
        else if (info->action == SessionCallbackInfo::SetMinRx)
          beacon->SetDefMinRxInterval(info->setValue);
        else if (info->action == SessionCallbackInfo::SetCPI)
          beacon->SetDefControlPlaneIndependent(bool(info->setValue));
        else if (info->action == SessionCallbackInfo::SetAdminUpPoll)
          beacon->SetDefAdminUpPollWorkaround(bool(info->setValue));
        else
        {
          LogAssertFalse("Incorrect default action in doHandleSession");
        }
        return 1;
      }


      if (!findSessionIdList(beacon, info->sessionId, ids))
        return 0;

      for (idIt = ids.begin(); idIt != ids.end(); idIt++)
      {
        Session *session = beacon->FindSessionId(*idIt);
        if (!session)
        {
          LogAssertFalse("No matching session for Id.");
          continue;
        }

        if (info->action == SessionCallbackInfo::State)
        {
          if (info->state == bfd::State::Down)
            session->ForceDown(bfd::Diag::Value(info->setValue));
          else if (info->state == bfd::State::AdminDown)
            session->ForceAdminDown(bfd::Diag::Value(info->setValue));
          else if (info->state == bfd::State::Up)
            session->AllowStateChanges();
          else
          {
            LogAssertFalse("Incorrect state in doHandleSession");
          }
        }
        else if (info->action == SessionCallbackInfo::Kill)
        {
          beacon->KillSession(session);
          session = NULL; // warning session now invalid
        }
        else if (info->action == SessionCallbackInfo::Reset)
        {
          bool active = session->IsActiveSession();
          IpAddr remoteAddr = session->GetRemoteAddress();
          IpAddr localAddr = session->GetLocalAddress();

          beacon->KillSession(session);
          session = NULL; // warning session now invalid
          gLog.Optional(Log::SessionDetail, "Reset session id=%u for local %s to remote %s.", *idIt, localAddr.ToString(), remoteAddr.ToString());
          if (active)
            beacon->StartActiveSession(remoteAddr, localAddr);
        }
        else if (info->action == SessionCallbackInfo::Suspend)
          session->SetSuspend(true);
        else if (info->action == SessionCallbackInfo::Resume)
          session->SetSuspend(false);
        else if (info->action == SessionCallbackInfo::SetMulti)
          session->SetMulti(uint8_t(info->setValue));
        else if (info->action == SessionCallbackInfo::SetMinTx)
          session->SetMinTxInterval(info->setValue);
        else if (info->action == SessionCallbackInfo::SetMinRx)
          session->SetMinRxInterval(info->setValue);
        else if (info->action == SessionCallbackInfo::SetCPI)
          session->SetControlPlaneIndependent(bool(info->setValue));
        else if (info->action == SessionCallbackInfo::SetAdminUpPoll)
          session->SetAdminUpPollWorkaround(bool(info->setValue));
        else
        {
          LogAssertFalse("Incorrect action in doHandleSession");
        }
      }

      return 1;
    }

    /**
     * Parses two message parameters that represent a time value.
     * Will put up an error message if no time specifier is found.
     *
     * @param valueString - The message parameter for the integer part of the time.
     * @param outTime - The result in microseconds.
     * @param notIntReply - The message used if value is not an int. Use %s for the
     *                    value that was given.
     *
     * @return bool - false on failure.
     */
    bool parseTimeValue(const char *valueString, uint32_t &outMicroTime, const char *notIntReply)
    {
      int64_t value;
      const char *unitString;
      static const char *unitvalues = "'s', 'ms' or 'us'";

      if (!valueString)
      {
        messageReplyF(notIntReply, "none");
        return false;
      }


      if (!StringToInt(valueString, value))
      {
        messageReplyF(notIntReply, valueString);
        return false;
      }

      if (value < 0)
      {
        messageReply("Negative values not allowed\n");
        return false;
      }

      unitString = getNextParam(valueString);
      if (!unitString)
      {
        messageReplyF("Must supply a unit after the value %s: %s\n", valueString, unitvalues);
        return false;
      }
      if (0 == strcmp(unitString,  "s"))
      {
        if (value > UINT32_MAX / 1000000U)
        {
          messageReplyF("Value <%s> seconds is too large to be converted to microseconds.\n", valueString);
          return false;
        }
        outMicroTime = (uint32_t)(value * 1000000U);
        return true;
      }
      else if (0 == strcmp(unitString,  "ms"))
      {
        if (value > UINT32_MAX / 1000U)
        {
          messageReplyF("Value <%s> milliseconds is too large to be converted to microseconds.\n", valueString);
          return false;
        }
        outMicroTime = (uint32_t)(value * 1000U);
        return true;
      }
      else if (0 == strcmp(unitString,  "us"))
      {
        if (value > UINT32_MAX)
        {
          messageReplyF("Value <%s> microseconds is too large.\n", valueString);
          return false;
        }
        outMicroTime = (uint32_t)(value);
        return true;
      }

      messageReplyF("Unknown unit <%s>. Use: %s\n", unitString, unitvalues);
      return false;
    }



    /**
     * Helper for handle_Session when action is "set"
     *
     * @param setting
     * @param info [out] - filled on success.
     *
     * @return bool - false on parse failure.
     */
    bool getSessionSetParams(const char *setting, SessionCallbackInfo &info)
    {
      static const char *commands = "'mintx', 'minrx', 'multi', 'cpi' or 'admin_up_poll'";
      const char *valueString;

      if (!setting)
      {
        messageReplyF("Must supply item to set: %s.\n", commands);
        return false;
      }

      else if (0 == strcmp(setting, "mintx"))
      {
        info.action = SessionCallbackInfo::SetMinTx;
        valueString = getNextParam(setting);
        if (!valueString)
        {
          messageReply("Must supply value for 'set mintx'.\n");
          return false;
        }
        if (!parseTimeValue(valueString, info.setValue, "'set mintx' value must be an integer followed by time unit : <%s>.\n"))
          return false;

        if (info.setValue == 0)
        {
          messageReply("'set mintx' value can not be 0.\n");
          return false;
        }
        messageReplyF("Attempting to set mintx to %s us.\n", FormatInteger(info.setValue));
        return true;
      }
      else if (0 == strcmp(setting, "minrx"))
      {
        info.action = SessionCallbackInfo::SetMinRx;
        valueString = getNextParam(setting);
        if (!valueString)
        {
          messageReply("Must supply value for 'set minrx'.\n");
          return false;
        }
        if (!parseTimeValue(valueString, info.setValue, "'set minrx' value must be an integer followed by time unit : <%s>.\n"))
          return false;
        messageReplyF("Attempting to set minrx to %s us.\n", FormatInteger(info.setValue));
        return true;
      }
      else if (0 == strcmp(setting, "multi"))
      {
        int64_t val64;

        info.action = SessionCallbackInfo::SetMulti;
        valueString = getNextParam(setting);
        if (!valueString)
        {
          messageReply("Must supply value for 'set multi'.\n");
          return false;
        }
        if (!StringToInt(valueString, val64))
        {
          messageReply("Must supply an non-zero integer value for 'set multi'.\n");
          return false;
        }

        if (val64 > UINT8_MAX)
        {
          messageReply("Value for 'set multi is too large..\n");
        }
        else if (val64 == 0)
        {
          messageReply("'set multi' value can not be 0.\n");
          return false;
        }
        info.setValue = uint32_t(val64);
        messageReplyF("Attempting to set multi to %u.\n", info.setValue);
        return true;
      }
      else if (0 == strcmp(setting, "cpi"))
      {
        info.action = SessionCallbackInfo::SetCPI;
        valueString = getNextParam(setting);
        if (!valueString)
        {
          messageReply("Must supply 'yes' or 'no' for 'set cpi'.\n");
          return false;
        }
        if (0 == strcmp(valueString,  "yes"))
          info.setValue = true;
        else if (0 == strcmp(valueString,  "no"))
          info.setValue = false;
        else
        {
          messageReplyF("Must supply 'yes' or 'no' for 'set cpi'. Unknown value <%s>.\n", valueString);
          return false;
        }
        messageReplyF("Attempting to set control plane independent (C) bit to %s.\n", valueString);
        return true;
      }
      else if (0 == strcmp(setting, "admin_up_poll"))
      {
        info.action = SessionCallbackInfo::SetAdminUpPoll;
        valueString = getNextParam(setting);
        if (!valueString)
        {
          messageReply("Must supply 'yes' or 'no' for 'set admin_up_poll'.\n");
          return false;
        }
        if (0 == strcmp(valueString,  "yes"))
          info.setValue = true;
        else if (0 == strcmp(valueString,  "no"))
          info.setValue = false;
        else
        {
          messageReplyF("Must supply 'yes' or 'no' for 'set admin_up_poll'. Unknown value <%s>.\n", valueString);
          return false;
        }
        messageReplyF("Attempting to %s admin_up_poll workaround.\n", info.setValue ? "enable" : "disable");
        return true;
      }
      else
      {
        messageReplyF("Unrecognized item to set <%s> use %s.\n", setting, commands);
        return false;
      }
    }

    /**
     * Helper for handle_Session when action is "state"
     *
     * @param setting
     * @param info [out] - filled on success.
     *
     * @return bool - false on parse failure.
     */
    bool getSessionStateParams(const char *setting, SessionCallbackInfo &info)
    {
      static const char *commands = "'up', 'admin', or 'down'";

      info.action = SessionCallbackInfo::State;

      if (!setting)
      {
        messageReplyF("Must supply state: %s.\n", commands);
        return false;
      }

      if (0 == strcmp(setting, "up"))
      {
        info.state = bfd::State::Up;
        info.setValue = bfd::Diag::None;
      }
      else if (0 == strcmp(setting, "down"))
      {
        info.state = bfd::State::Down;
        info.setValue = bfd::Diag::PathDown;
      }
      else if (0 == strcmp(setting, "admin"))
      {
        info.state = bfd::State::AdminDown;
        info.setValue = bfd::Diag::AdminDown;
      }
      else
      {
        messageReplyF("Unrecognized state <%s> use %s.\n", setting, commands);
        return false;
      }


      // Get diagnostic
      const char *diagString =  getNextParam(setting);
      if (diagString)
      {
        int64_t value;

        if (info.state == bfd::State::Up)
        {
          messageReply("State up can not have diagnostic value.\n");
          return false;
        }

        if (!StringToInt(diagString, value))
        {
          messageReplyF("Unrecognized diagnostic value. Must be integer <%s>.\n", diagString);
          return false;
        }

        if (value < 0 || value > bfd::Diag::MaxDiagnostic)
        {
          messageReplyF("Diagnostic value. Must be integer between 0 and %u.\n", uint32_t(bfd::Diag::MaxDiagnostic));
          return false;
        }

        info.setValue = uint32_t(value);
      }

      return true;
    }

    /**
     * "session" command.
     * Format 'session' ip/id/all (state ['admin'|'down'|'up'])|reset|stop|kill
     */
    void handle_Session(const char *message)
    {
      const char *whichString, *actionString;
      const char *idOptions = "'all', 'new', session id or 'remote ip local ip'";
      SessionCallbackInfo info;
      intptr_t result;
      bool isSetting = false;

      whichString = getNextParam(message);
      if (!whichString)
      {
        messageReplyF("Must supply %s.\n", idOptions);
        return;
      }

      if (0 == strcmp(whichString, "new"))
      {
        info.defSetting = true;
      }
      else
      {
        string error;
        info.defSetting = false;
        if (!paramToIdOrIp(&whichString, info.sessionId, error))
        {
          messageReplyF("Must supply %s before other settings. %s\n", idOptions,  error.c_str());
          return;
        }
      }

      static const char *actions = "'state', 'set', 'kill', 'reset', 'suspend' or 'resume'";

      actionString = getNextParam(whichString);
      if (!actionString)
      {
        messageReplyF("Must supply session action: %s.\n", actions);
        return;
      }

      if (0 == strcmp(actionString, "state"))
      {
        if (!getSessionStateParams(getNextParam(actionString), info))
          return;
        messageReplyF("Attempting to put session(s) into %s state with diagnostic <%s>.\n",
                      bfd::StateName(info.state),
                      bfd::DiagString(bfd::Diag::Value(info.setValue)));
      }
      else if (0 == strcmp(actionString, "reset"))
      {
        info.action = SessionCallbackInfo::Reset;
        messageReplyF("Attempting to %s session(s).\n", actionString);
      }
      else if (0 == strcmp(actionString, "suspend"))
      {
        info.action = SessionCallbackInfo::Suspend;
        messageReplyF("Attempting to %s session(s).\n", actionString);
      }
      else if (0 == strcmp(actionString, "resume"))
      {
        info.action = SessionCallbackInfo::Resume;
        messageReplyF("Attempting to %s session(s).\n", actionString);
      }
      else if (0 == strcmp(actionString, "kill"))
      {
        info.action = SessionCallbackInfo::Kill;
        messageReplyF("Attempting to %s session(s).\n", actionString);
      }
      else if (0 == strcmp(actionString, "set"))
      {
        isSetting = true;
        if (!getSessionSetParams(getNextParam(actionString), info))
          return;
      }
      else
      {
        messageReplyF("Unrecognized session action <%s> use: %s.\n", actionString, actions);
        return;
      }

      if (info.defSetting && !isSetting)
      {
        messageReply("'new' can only be used with 'set'.\n");
        return;
      }

      if (doBeaconOperation(&CommandProcessorImp::doHandleSession, &info, &result))
      {
        if (!result)
        {
          reportNoSuchSession(info.sessionId);
          return;
        }
      }
    }

    intptr_t doHandleConsumeBeacon(Beacon *ATTR_UNUSED(beacon), void *userdata)
    {
      int64_t index;
      int64_t val64 = *reinterpret_cast<int64_t *>(userdata);

      try
      {
        for (index = 0; index < val64; index++)
        {
          char *unused = new char[1024];
          memset(unused, 0xfe, 1024);
        }
      }
      catch (std::exception &e)
      {
        return false;
      }

      return true;
    }


    /**
     * "test" command.
     * For internal testing only.
     * Format 'test' 'consume'  - consumes given amount of memory in KB blocks.
     *
     */
    void handle_Test(const char *message)
    {
      const char *itemString;
      static const char *itemValues = "'consume'";

      itemString = getNextParam(message);
      if (!itemString)
      {
        messageReplyF("Must specify: %s.\n", itemValues);
        return;
      }

      if (0 == strcmp(itemString, "consume"))
      {
        int64_t val64, index;
        const char *valueString = getNextParam(itemString);
        if (!valueString)
        {
          messageReply("Must supply number of 1K blocks to consume for 'test consume'.\n");
          return;
        }
        if (!StringToInt(valueString, val64) || val64 <= 0)
        {
          messageReply("Must supply an non-zero integer value for 'test consume'.\n");
          return;
        }


        messageReplyF("Consuming %"PRIi64"K memory.\n", val64);
        try
        {
          for (index = 0; index < val64; index++)
          {
            char *unused = new char[1024];
            memset(unused, 0xfe, 1024);
          }
        }
        catch (std::exception &e)
        {
          messageReplyF("Consume completed: %s\n",  e.what());
          return;
        }

        messageReplyF("Consumed %"PRIi64"K memory.\n", val64);
      }
      else if (0 == strcmp(itemString, "consume_beacon"))
      {
        intptr_t result;
        int64_t val64;
        const char *valueString = getNextParam(itemString);
        if (!valueString)
        {
          messageReply("Must supply number of 1K blocks to consume for 'test consume_beacon'.\n");
          return;
        }
        if (!StringToInt(valueString, val64) || val64 <= 0)
        {
          messageReply("Must supply an non-zero integer value for 'test consume_beacon'.\n");
          return;
        }

        if (doBeaconOperation(&CommandProcessorImp::doHandleConsumeBeacon, &val64, &result))
        {
          if (result)
            messageReplyF("Consumed %"PRIi64"K memory.\n", val64);
          else
            messageReplyF("Consumed %"PRIi64"K memory. Exception thrown.\n", val64);
        }
      }
      else
      {
        messageReplyF("'test' must be followed by one of %s. Unknown <%s>\n", itemValues, itemString);
      }
    }

    /**
     * Sends message back to control. Message must be verified for length.
     */
    void doMessageReply(const char *reply, size_t length)
    {
      // TODO timeout? Check for shutdown?
      m_replySocket.Send(reply, length);
    }


    /**
     * Sends message back to control app.
     * Should include \n if desired.
     */
    void messageReply(const char *reply)
    {
      size_t len;
      len = strlen(reply);

      if (len > MaxReplyLineSize)
      {
        gLog.Message(Log::Command, "Warning. Truncating message reply from %zu to %zu.", len, MaxReplyLineSize);
        len = MaxReplyLineSize;
      }
      doMessageReply(reply, len);
    }

    /**
     * Formatted reply to message.
     *
     */
    void messageReplyF(const char *format, ...) ATTR_FORMAT(printf, 2, 3)
    {
      va_list args;
      va_start(args, format);
      vsnprintf(&m_inReplyBuffer.front(), m_inReplyBuffer.size(), format, args);
      va_end(args);
      messageReply(&m_inReplyBuffer.front());
    }

    /**
     * Returns the next param in a double null terminated string list, or NULL if it
     * is the last one.
     *
     * @param param
     *
     * @return const char*
     */
    static const char* getNextParam(const char *param)
    {
      const char *next = param;
      while (*next)
        next++;

      if (next == param)
        return NULL;

      next++;
      if (!*next)
        return NULL;

      return next;
    }

    /**
     * Checks if a shutdown has been requested. Do not call while holding
     * m_mainLock.
     *
     *
     *
     * @return bool - True if a shutdown was requested.
     */
    bool isStopListeningRequested()
    {
      AutoQuickLock lock(m_mainLock, true);
      return m_stopListeningRequested;
    }

  }; // class CommandProcessorImp


  CommandProcessor* MakeCommandProcessor(Beacon &beacon)
  {
    return new CommandProcessorImp(beacon);
  }

}  // namespace
