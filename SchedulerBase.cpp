/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "SchedulerBase.h"
#include "utils.h"
#include "SmartPointer.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

using namespace std;


/**
 * A timer class for use with SchedulerBase
 *
 */
class TimerImpl : public Timer
{

private:
  SchedulerBase *m_scheduler;
  SchedulerBase::timer_set *m_activeTimers;  // Use only from main thread. The active timer list that this is part of when active.
  Timer::Callback m_callback;
  void *m_userdata;
  TimeSpec m_expireTime;
  TimeSpec m_startTime;  // only valid when not stopped.
  bool m_stopped;
  char *m_name;  // for logging
  Timer::Priority::Value m_priority;

public:
  TimerImpl(SchedulerBase &scheduler, SchedulerBase::timer_set *timerSet, const char *name) : Timer(),
     m_scheduler(&scheduler),
     m_activeTimers(timerSet),
     m_callback(NULL),
     m_userdata(NULL),
     m_stopped(true),
     m_name(NULL),
     m_priority(Timer::Priority::Hi)
  {
    if (name)
      m_name = strdup(name);
    else
    {
      char *tempName = (char *)malloc(32);
      snprintf(tempName, 32, "%p", this);
      m_name = tempName;
    }

    m_expireTime.tv_sec = 0;
    m_expireTime.tv_nsec = 0;
  };

  ~TimerImpl()
  {
    // This will remove it from the active list.
    Stop();
    if (m_name)
      free(m_name);
  };


  void SetCallback(Timer::Callback callback, void *userdata)
  {
    LogAssert(m_scheduler->IsMainThread());
    m_callback = callback;
    m_userdata = userdata;
  }

  void Stop()
  {

    LogAssert(m_scheduler->IsMainThread());

    if (m_stopped)
      LogOptional(Log::TimerDetail, "Stopping ignored on stopped timer %s", m_name);
    else
    {
      // Remove us from active timers list. (Must remove before changing timer.)
      SchedulerBase::timer_set_it found = SchedulerBase::TimeSetFindExact(*m_activeTimers, this);
      if (found != m_activeTimers->end())
        m_activeTimers->erase(found);

      m_stopped = true;
      LogOptional(Log::TimerDetail, "Stopping timer %s. (%zu timers)", m_name, m_activeTimers->size());
    }
  }



  bool SetMsTimer(uint32_t ms)
  {
    // no check for overflow. But that would be a lot of years ;-)
    return SetMicroTimer(uint64_t(ms) * 1000);
  }


  bool SetMicroTimer(uint64_t micro)
  {
    LogAssert(m_scheduler->IsMainThread());

    TimeSpec startTime(TimeSpec::MonoNow());

    if (startTime.empty())
      return false;

    return setExpireTime(startTime, micro);
  }


  bool UpdateMicroTimer(uint64_t micro)
  {
    LogAssert(m_scheduler->IsMainThread());

    if (IsStopped())
      return SetMicroTimer(micro);

    return setExpireTime(m_startTime, micro);
  }



  bool IsStopped() const
  {
    LogAssert(m_scheduler->IsMainThread());
    return m_stopped;
  }

  void SetPriority(Timer::Priority::Value pri)
  {
    LogAssert(m_scheduler->IsMainThread());
    m_priority = pri;
  }

  Timer::Priority::Value GetPriority()
  {
    LogAssert(m_scheduler->IsMainThread());
    return m_priority;
  }

  /**
   * Get the time that this will expire.
   *
   * @note Call only on main thread. See IsMainThread().
   *
   * @return const struct TimeSpec&
   */
  const TimeSpec& GetExpireTime() const
  {
    return m_expireTime;
  }

  /**
   * Called by the Scheduler to mark the timer as stopped and run its action. Will
   * Remove the timer from the active list.
   *
   */
  void ExpireTimer()
  {
    LogAssert(!m_stopped);

    Stop();

    LogOptional(Log::TimerDetail, "Expired timer %s calling callback", m_name);
    m_callback(this, m_userdata);
  }

  /**
   * Gets the name
   *
   * @return const char*
   */
  const char* Name()
  {
    return  m_name;
  }

private:

  /**
   * Changes the start and expire time for the timer.
   *
   * @param startTime - The time of the last timer start. May be m_startTime.
   * @param micro - Time to expire from startTime in microseconds.
   *
   * @return bool - false on failure.
   */
  bool setExpireTime(const struct timespec &startTime,  uint64_t micro)
  {
    bool expireChange, startChange;
    TimeSpec expireTime(startTime);

    expireTime += TimeSpec(TimeSpec::Microsec, micro);

    startChange = (m_startTime != startTime);
    expireChange = (m_stopped || m_expireTime != expireTime);

    //LogOptional(Log::Temp, "Timer %s before change %zu items",m_name, m_activeTimers->size());

    if (!expireChange && !startChange)
    {
      LogOptional(Log::TimerDetail, "Timer %s no change.  %" PRIu64 "  microseconds. Expires:%jd:%09ld", m_name, micro, (intmax_t)expireTime.tv_sec, expireTime.tv_nsec);
      return true;
    }

    LogOptional(Log::TimerDetail, "%s timer %s for %" PRIu64 " microseconds from %jd:%09ld. Expires:%jd:%09ld",
                m_stopped ? "Starting" : startChange ? "Resetting" : "Advancing",
                m_name,
                micro,
                (intmax_t)startTime.tv_sec,
                startTime.tv_nsec,
                (intmax_t)expireTime.tv_sec,
                expireTime.tv_nsec
               );

    // Start time does not effect sorting in active timer list, so we can set it now.
    // Stopped also should not matter.
    if (startChange)
      m_startTime = startTime;
    m_stopped = false;

    if (expireChange)
    {
      SchedulerBase::timer_set_it found = SchedulerBase::TimeSetFindExact(*m_activeTimers, this);
      if (found != m_activeTimers->end())
      {
        if (!willTimerBeSorted(found, expireTime))
        {
          m_activeTimers->erase(found);
          found = m_activeTimers->end();  // cause it to be added back
        }
      }

      // Actually set the time
      m_expireTime = expireTime;

      if (found == m_activeTimers->end())
      {
        try
        {
          m_activeTimers->insert(this);
        }
        catch (std::exception &e)
        {
          m_stopped = true;
          gLog.Message(Log::Error, "Failed to add timer: %s.", e.what());
          return false;
        }
      }
    }

    //LogOptional(Log::Temp, "Timer %s after change %zu items",m_name, m_activeTimers->size());
    return true;
  }


  /**
   * Checks if the timer item is still in proper order.
   *
   * @param item [in] - an iterator to the item to check in  m_activeTimers-> Must
   *             be a valid item (not end())
   * @param expireTime [in] - The proposed new expire time.
   *
   * @return bool - true if the item is would still be in sorted order.
   */
  bool willTimerBeSorted(const SchedulerBase::timer_set_it item, const struct timespec &expireTime)
  {
    SchedulerBase::timer_set_it temp;

    if (item != m_activeTimers->begin())
    {
      // We want the timer to be later than, or equal to, the previous item
      if (0 > timespecCompare(expireTime, (*(--(temp = item)))->GetExpireTime()))
        return false;
    }

    (temp = item)++;
    if (temp != m_activeTimers->end())
    {
      // we want the timer to be earlier or equal to the next item.
      if (0 < timespecCompare(expireTime, (*temp)->GetExpireTime()))
        return false;
    }
    return true;
  }
};



SchedulerBase::SchedulerBase() : Scheduler(),
   m_isStarted(false),
   m_wantsShutdown(false),
   m_activeTimers(compareTimers),
   m_timerCount(0)
{
  m_mainThread = pthread_self();
}

SchedulerBase::~SchedulerBase()
{
  for (SignalItemHashMap::iterator sig = m_signals.begin(); sig != m_signals.end(); sig++)
  {
    ::close(sig->second.fdRead);
    ::close(sig->second.fdWrite);
  }
}

bool SchedulerBase::Run()
{
  uint32_t iter = 0;
  TimeSpec timeout;
  TimeSpec immediate;
  bool gotEvents;

  if (!LogVerify(IsMainThread()))
    return false;

  m_isStarted = true;

  // Start with a quick event check.
  timeout = immediate;
  while (true)
  {
    iter++;

    if (m_wantsShutdown)
      break;

    //
    //  Get event, or timeout.
    //
    gLog.Optional(Log::TimerDetail, "checking events (%u)", iter);
    gotEvents = waitForEvents(timeout);

    // By default the next event check is immediately.
    timeout = immediate;

    //
    // High priority timers, if any, get handled now
    //
    while (!m_wantsShutdown && expireTimer(Timer::Priority::Hi))
    { //nothing
    }

    if (m_wantsShutdown)
      break;

    //
    //  Handle any events.
    //
    if (gotEvents)
    {
      int socketId;

      gLog.Optional(Log::TimerDetail, "Handling events (%u)", iter);


      while (-1 != (socketId = getNextSocketEvent()))
      {
        // we have a socket event .. is it a socket or a signal?

        SocketItemHashMap::iterator foundSocket;
        SignalItemHashMap::iterator foundSignal;

        if (m_sockets.end() != (foundSocket = m_sockets.find(socketId)))
        {
          if (LogVerify(foundSocket->second.callback != NULL))
            foundSocket->second.callback(socketId, foundSocket->second.userdata);
        }
        else if (m_signals.end() != (foundSignal = m_signals.find(socketId)))
        {
          if (LogVerify(foundSignal->second.callback != NULL))
          {
            // 'Drain' the pipe.
            char drain[128];
            int result;
            size_t reads = 0;

            while (0 < (result = ::read(socketId, drain, sizeof(drain))))
              reads++;

            if (reads == 0 && result < 0)
              gLog.LogError("Failed to read from pipe %d: %s", socketId, ErrnoToString());
            else if (result == 0)
              gLog.LogError("Signaling pipe write end for %d closed", socketId);

            foundSignal->second.callback(foundSignal->second.fdWrite, foundSignal->second.userdata);
          }
        }
        else
        {
          gLog.Optional(Log::TimerDetail, "Socket (%d) signaled with no handler (%u).", socketId, iter);
        }

        if (m_wantsShutdown)
          break;
      }

      if (m_wantsShutdown)
        break;
    }

    //
    //  Handle a low priority timer if there are no events.
    //  TODO: starvation is a potential problem for low priority timers.
    //
    if (!gotEvents && !expireTimer(Timer::Priority::Low))
    {
      // No events and no more timers, so we are ready to sleep again.
      timeout = getNextTimerTimeout();
    }

    if (m_wantsShutdown)
      break;
  } // while true

  return true;
}


/**
 * Helper for Run().
 *
 * Gets the timeout period between now and the next timer.
 *
 * @return - The timeout value
 */
TimeSpec SchedulerBase::getNextTimerTimeout()
{
  //
  //  Calculate next scheduled timer time.
  //
  if (m_activeTimers.empty())
  {
    // Just for laughs ... and because we do not run on low power machines, wake up
    // every few seconds.
    return TimeSpec(3, 0);
  }

  TimeSpec now(TimeSpec::MonoNow());

  if (now.empty())
    return TimeSpec(TimeSpec::Millisec, 200); // 200 ms?

  TimeSpec result = (*m_activeTimers.begin())->GetExpireTime() - now;
  if (result.IsNegative())
    return TimeSpec();
  return result;
}

/**
 *
 * Helper for Run().
 * Expires the next timer with priority of minPri or higher.
 *
 * @return - false if there are no more timers.
 */
bool SchedulerBase::expireTimer(Timer::Priority::Value minPri)
{
  TimeSpec now(TimeSpec::MonoNow());

  if (now.empty())
    return false;

  for (timer_set_it nextTimer = m_activeTimers.begin(); nextTimer != m_activeTimers.end(); nextTimer++)
  {
    TimerImpl *timer = *nextTimer;

    if (0 < timespecCompare(timer->GetExpireTime(), now))
      return false;  // non-expired timer ... we are done!

    if (timer->GetPriority() >= minPri)
    {
#ifdef BFD_TEST_TIMERS
      TimeSpec dif = now -  timer->GetExpireTime();
      gLog.Optional(Log::Temp, "Timer %s is off by %.4f ms",
                    timer->Name(),
                    timespecToSeconds(dif) * 1000.0);
#endif

      // Expire the timer, which will run the action.
      // Note that the action could also modify the m_activeTimers list.
      timer->ExpireTimer();
      return true;
    }
  }

  return false;
}

bool SchedulerBase::IsMainThread()
{
  return(bool)pthread_equal(m_mainThread, pthread_self());
}


bool SchedulerBase::SetSocketCallback(int socket, Scheduler::SocketCallback callback, void *userdata)
{
  LogAssert(IsMainThread());

  if (!LogVerify(callback) || !LogVerify(socket != -1))
    return false;

  if (!watchSocket(socket))
    return false;

  schedulerSocketItem item;

  item.callback = callback;
  item.userdata = userdata;
  item.socket = socket;

  m_sockets[socket] = item;

  return true;
}

void SchedulerBase::RemoveSocketCallback(int socket)
{
  LogAssert(IsMainThread());

  SocketItemHashMap::iterator foundSocket;

  foundSocket = m_sockets.find(socket);


  if (foundSocket == m_sockets.end())
  {
    gLog.LogError("RemoveSocketCallback called with unknown socket %d", socket);
    return;
  }

  m_sockets.erase(foundSocket);

  unWatchSocket(socket);
}


bool SchedulerBase::CreateSignalChannel(int *outSigId, SignalCallback callback, void *userdata)
{
  LogAssert(IsMainThread());

  if (!LogVerify(outSigId) || !LogVerify(callback))
    return false;

  *outSigId = -1;

  // Create a set of pipes
  int fdPipe[2];
  int flags;
  int ret = pipe(fdPipe);

  if (ret != 0)
  {
    gLog.ErrnoError(errno, "Unable to create pipe for signaling");
    return false;
  }

  FileDescriptor pipeRead(fdPipe[0]);
  FileDescriptor pipeWrite(fdPipe[1]);

  flags = fcntl(pipeRead, F_GETFL);
  flags = flags | O_NONBLOCK;
  if (-1 == fcntl(pipeRead, F_SETFL, flags))
  {
    gLog.LogError("Failed to set read pipe to non-blocking.");
    return false;
  }
  flags = fcntl(pipeWrite, F_GETFL);
  flags = flags | O_NONBLOCK;
  if (-1 == fcntl(pipeWrite, F_SETFL, flags))
  {
    gLog.LogError("Failed to set write pipe to non-blocking.");
    return false;
  }

  if (!watchSocket(pipeRead))
    return false;

  schedulerSignalItem item;

  item.callback = callback;
  item.userdata = userdata;
  item.fdWrite = pipeWrite;
  item.fdRead = pipeRead;

  m_signals[item.fdRead] = item;

  pipeWrite.Detach();
  pipeRead.Detach();

  *outSigId = item.fdWrite;

  gLog.Optional(Log::TimerDetail, "Created signal channel from %d to %d .", item.fdWrite, item.fdRead);

  return true;
}

bool SchedulerBase::Signal(int sigId)
{
  char sig = 'x';

  if (1 != ::write(sigId, &sig, 1))
  {
    gLog.LogError("Failed to signal on pipe %d: %s", sigId, ErrnoToString());
    return false;
  }

  return true;
}


void SchedulerBase::RemoveSignalChannel(int sigId)
{
  LogAssert(IsMainThread());

  SignalItemHashMap::iterator foundSignal;

  for (SignalItemHashMap::iterator sig = m_signals.begin(); sig != m_signals.end(); sig++)
  {
    if (sig->second.fdWrite == sigId)
    {
      int readPipe = sig->second.fdRead;
      int writePipe = sig->second.fdWrite;
      m_signals.erase(sig);
      unWatchSocket(readPipe);
      ::close(readPipe);
      ::close(writePipe);
      return;
    }
  }

  gLog.LogError("RemoveSignalChannel called with unknown signal %d", sigId);
}

void SchedulerBase::RequestShutdown()
{
  LogAssert(IsMainThread());
  m_wantsShutdown = true;
}


Timer* SchedulerBase::MakeTimer(const char *name)
{
  m_timerCount++;
  return new TimerImpl(*this, &m_activeTimers, name);
}

/**
 * Call when completely done with a timer.
 *
 * @param timer
 */
void SchedulerBase::FreeTimer(Timer *timer)
{
  if (timer)
  {
    TimerImpl *theTimer = static_cast<TimerImpl *>(timer);

    m_timerCount--;
    delete theTimer;
  }

}

// Return true if lhs should be before rhs.
bool SchedulerBase::compareTimers(const TimerImpl *lhs, const TimerImpl *rhs)
{
  // Stopped timers should not be in the list.
  LogAssert(!lhs->IsStopped() && !rhs->IsStopped());


  // We want the "earliest" timers first. So we return true if lhs is earlier than
  // rhs. -1 if left is earlier.
  return (0 > timespecCompare(lhs->GetExpireTime(), rhs->GetExpireTime()));
}

/**
 * Finds a TimerImpl in the timer_set.
 *
 * Note that timer_set::find() would return any timer that matches, which is
 * defined as matching expire time. This function will **only** return an
 * iterator that points to the specific TimerImpl provided.
 *
 *
 * @param timerSet [in] - The timer set to search.
 * @param target [in] - The object to search for.
 *
 * @return SchedulerBase::timer_set_it - An iterator to the timer, or
 *      timerSet.end() if no match was found.
 *
 */
SchedulerBase::timer_set_it SchedulerBase::TimeSetFindExact(SchedulerBase::timer_set &timerSet,
                                                            TimerImpl *target)
{
  std::pair<timer_set_it, timer_set_it> range;

  range = timerSet.equal_range(target);

  for (timer_set_it it = range.first; it != range.second; ++it)
  {
    // Compare as pointers
    if (target == *it)
      return it;
  }
  return timerSet.end();
}
