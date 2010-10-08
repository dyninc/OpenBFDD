/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "Scheduler.h"
#include "utils.h"
#include <errno.h>
#include <signal.h>
#include <ext/hash_map>
#include <string.h>
#include <set>
#ifdef HAVE_KEVENT
#include <sys/event.h>
#endif

using namespace std;
using namespace __gnu_cxx;  // ughh! but easier than finding a good hash implementation.

#ifndef HAVE_KEVENT
#warning TEMPORARY to compile without kevent ... will not actually run!
struct kevent {
	uintptr_t	ident;		/* identifier for this event */
	short		filter;		/* filter for event */
	u_short		flags;
	u_int		fflags;
	intptr_t	data;
	void		*udata;		/* opaque user data identifier */
};

int kqueue() {return 0;}
int kevent(int kq, const struct kevent *changelist, int nchanges,
	 struct kevent *eventlist, int nevents,
	 const struct timespec *timeout) {return 0;}

#define EVFILT_READ		(-1)
#define EVFILT_WRITE		(-2)
#define EVFILT_AIO		(-3)	/* attached to aio requests */
#define EVFILT_VNODE		(-4)	/* attached to vnodes */
#define EVFILT_PROC		(-5)	/* attached to struct proc */
#define EVFILT_SIGNAL		(-6)	/* attached to struct proc */
#define EVFILT_TIMER		(-7)	/* timers */
/*	EVFILT_NETDEV		(-8)	   no longer supported */
#define EVFILT_FS		(-9)	/* filesystem events */
#define EVFILT_LIO		(-10)	/* attached to lio requests */
#define EVFILT_USER		(-11)	/* User events */
#define EVFILT_SYSCOUNT		11

#define EV_SET(kevp_, a, b, c, d, e, f) do {	\
	struct kevent *kevp = (kevp_);		\
	(kevp)->ident = (a);			\
	(kevp)->filter = (b);			\
	(kevp)->flags = (c);			\
	(kevp)->fflags = (d);			\
	(kevp)->data = (e);			\
	(kevp)->udata = (f);			\
} while(0)


/* actions */
#define EV_ADD		0x0001		/* add event to kq (implies enable) */
#define EV_DELETE	0x0002		/* delete event from kq */
#define EV_ENABLE	0x0004		/* enable event */
#define EV_DISABLE	0x0008		/* disable event (not reported) */

/* flags */
#define EV_ONESHOT	0x0010		/* only report one occurrence */
#define EV_CLEAR	0x0020		/* clear event state after reporting */
#define EV_RECEIPT	0x0040		/* force EV_ERROR on success, data=0 */
#define EV_DISPATCH	0x0080		/* disable event after reporting */

#define EV_SYSFLAGS	0xF000		/* reserved by system */
#define EV_FLAG1	0x2000		/* filter-specific flag */

/* returned values */
#define EV_EOF		0x8000		/* EOF detected */
#define EV_ERROR	0x4000		/* error, data contains errno */


#endif


namespace openbfdd
{
  class SchedulerImpl;


  class SchedulerImpl : public Scheduler
  {
  protected:
    class TimerImpl;  // forward declare for timer_set.

  public:
    typedef  multiset<TimerImpl *, bool(*)(const TimerImpl *,const TimerImpl *)> timer_set;
    typedef  timer_set::iterator timer_set_it;

  private:

    struct schedulerSignalCallback
    {
      Scheduler::SignalCallback callback;
      void * userdata;
    };
    typedef  hash_map<int, schedulerSignalCallback> signal_hash_map;

    pthread_t m_mainThread; // This is the thread under which Run was called.

    //
    // Only access these from the Main scheduler thread. See IsMainThread().
    // 
    bool m_isStarted; // True if Run was called. 
    Scheduler::SocketCallback m_socketCallback;
    void *m_socketCallbackUserData;
    int m_inSock;
    int m_kqueue;
    signal_hash_map m_signalCallbacks;
    bool m_wantsShutdown;
    timer_set m_activeTimers; 
    int m_timerCount;   // only used for desbugging

  public:
    SchedulerImpl() : Scheduler(),
    m_isStarted(false),
    m_socketCallback(NULL),
    m_socketCallbackUserData(NULL),
    m_inSock(-1),
    m_signalCallbacks(32),
    m_wantsShutdown(false),
    m_activeTimers(compareTimers),
    m_timerCount(0)
    {
      m_mainThread = pthread_self();
      m_kqueue = ::kqueue();
      if (m_kqueue < 0)
        gLog.Message(Log::Critical, "Failed to create Scheduler kqueue. Can not proceed.");
    }

    virtual ~SchedulerImpl()
    {
    }

    bool Run()
    {
      struct kevent events[10];
      uint32_t iter=0;
      int eventCount;
      struct timespec timeout;
      struct timespec immediate = {0,0};

      if (!LogVerify(IsMainThread()))
        return false;

      if (!LogVerify(m_kqueue!=-1))
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
        eventCount = ::kevent(m_kqueue, NULL, 0, events, countof(events), &timeout);
        if (eventCount < 0)
          gLog.LogError("kevent failed (%u) : %s", iter, strerror(errno));
        else if (eventCount == 0)
        {
          if (timeout.tv_nsec != 0 || timeout.tv_sec != 0)
            gLog.Optional(Log::TimerDetail, "kevent timeout (%u)", iter);
        }
        else
          gLog.Optional(Log::TimerDetail, "kevent received %d events (%u)", eventCount, iter);

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
        if (eventCount > 0)
        {
          gLog.Optional(Log::TimerDetail, "Handling %d events (%u)", eventCount, iter);
          if (!handleKEvents(events, eventCount, iter))
            break;
        }

        // 
        //  Handle a low priority timer if there are no events.
        //  TODO: starvation is a potential problem for low priority timers.
        // 
        if (eventCount == 0 && !expireTimer(Timer::Priority::Low))
        {
          // No events and no more timers, so we are ready to sleep again. 
          getNextTimerTimeout(timeout);
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
     * @param timeout 
     */
    void getNextTimerTimeout(struct timespec &timeout)
    {
      struct timespec now;

      // 
      //  Calculate next scheduled timer time. 
      // 
      if (!m_activeTimers.empty())
      {
        if (!GetMonolithicTime(now))
          timeout.tv_nsec = 200000000;  // 200 ms?
        else
        {
          timespecSubtract(timeout,  (*m_activeTimers.begin())->GetExpireTime(),  now);
          if (timespecIsNegative(timeout))
          {
            timeout.tv_sec = 0;
            timeout.tv_nsec = 0;
          }
        }
      }
      else
      {
        // Just for laughs ... and because we do not run on low power machines, wake up
        // every few seconds.
        timeout.tv_sec = 3;
        timeout.tv_nsec = 0;
      }
    }


    /** 
     *  
     * Helper for Run(). 
     * Expires the next timer with priority of minPri or higher. 
     *  
     * @return - false if there are no more timers.
     */
    bool expireTimer(Timer::Priority::Value minPri)
    {
      struct timespec now;

      if (!GetMonolithicTime(now))
        return false;

      for (timer_set_it nextTimer = m_activeTimers.begin(); nextTimer != m_activeTimers.end(); nextTimer++)
      {
        TimerImpl *timer = *nextTimer; 

        if ( 0 < timespecCompare(timer->GetExpireTime(), now))
          return false;  // non-expired timer ... we are done!

        if (timer->GetPriority() >= minPri)
        {
          // Expire the timer, which will run the action.
          // Note that the action could also modify with the m_activeTimers list.
          timer->ExpireTimer();
          return true;
        }
      }
      
      return false;
    }

    /**
     * Helper for Run(). Processes the reult of a kevent call.
     * 
     * @param events 
     * @param eventCount 
     * @param iter - for logging. 
     *  
     * @return - false if m_wantsShutdown.
     */
    bool handleKEvents(struct kevent *events, int eventCount, int iter)
    {
      for (int which = 0; which < eventCount; which++)
      {
        // TODO check for EV_ERROR in flags?
        if (events[which].filter == EVFILT_READ)
        {
          // We have a socket event
          LogAssert(events[which].ident == (uintptr_t)m_inSock);
          if (m_socketCallback)
            m_socketCallback(m_inSock, m_socketCallbackUserData);
        }
        else if (events[which].filter == EVFILT_SIGNAL)
        {
          // we have a signal event
          int sigId = (int)events[which].ident;
          signal_hash_map::iterator found =  m_signalCallbacks.find(sigId);
          if (found == m_signalCallbacks.end())
          {
            gLog.Optional(Log::TimerDetail, "kevent signal %s (%d) received with no handler (%u).",strsignal(sigId), sigId, iter);
          }
          else if (found->second.callback == NULL)
          {
            LogAssertFalse("empty callback");
          }
          else
            found->second.callback(sigId, found->second.userdata);
        }

        // Since control was yielded we may have a shutdown request. For now we do not
        // handle the rest of the events.
        if (m_wantsShutdown)
          return false;
      }

      return !m_wantsShutdown;
    }

    bool IsMainThread()
    {
      return(bool)pthread_equal(m_mainThread, pthread_self());
    }


    bool SetSocketCallback(int socket, Scheduler::SocketCallback callback, void *userdata)
    {
      struct kevent change;

      LogAssert(IsMainThread());
      LogAssert(m_inSock == -1);
      LogAssert(m_socketCallback == NULL);
      LogAssert(m_kqueue!=-1);

      if (!callback || socket == -1 || m_kqueue == -1)
        return false;


      EV_SET(&change, socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, NULL, NULL);
      if (kevent(m_kqueue, &change, 1, NULL, 0, NULL) < 0)
      {
        gLog.ErrnoError(errno, "Failed to add socket to kqueue");
        return false;
      }

      m_inSock = socket;
      m_socketCallback = callback;
      m_socketCallbackUserData = userdata;

      return true;
    }

    void RemoveSocketCallback(Scheduler::SocketCallback callback)
    {
      struct kevent change;

      LogAssert(IsMainThread());
      LogAssert(m_inSock != -1);
      LogAssert(m_socketCallback != NULL);
      LogAssert(m_kqueue!=-1);

      if (!m_socketCallback || m_inSock == -1 || m_kqueue == -1)
      {
        LogAssertFalse("RemoveSocketCallback called when not initialized");
        return;
      }

      if (m_socketCallback != callback)
      {
        LogAssertFalse("RemoveSocketCallback called with incorrect callback.");
        return;
      }

      EV_SET(&change, m_inSock, EVFILT_READ, EV_DELETE, 0, NULL, NULL);
      if (kevent(m_kqueue, &change, 1, NULL, 0, NULL) < 0)
        gLog.ErrnoError(errno, "Failed to remove socket to kqueue");

      m_inSock = -1;
      m_socketCallback = NULL;
      m_socketCallbackUserData = NULL;
    }

    bool SetSignalCallback(int sigId, SignalCallback callback, void *userdata, bool disable)
    {
      struct kevent change;

      LogAssert(IsMainThread());
      LogAssert(sigId > 0);
      LogAssert(m_kqueue> 0);

      if (!callback || sigId <= 0 || m_kqueue == -1)
        return false;


      // currently there should be no other handler in place.
      signal_hash_map::iterator found =  m_signalCallbacks.find(sigId);
      if (found != m_signalCallbacks.end())
      {
        LogAssertFalse("Callback already installed.");
        return false;
      }

      if (disable)
      {
        if (SIG_ERR == ::signal(sigId, SIG_IGN))
        {
          gLog.LogError("Failed to ignore signal %s (%d) adding handler to kqueue: %s", strsignal(sigId), sigId, strerror(errno));
          return false;
        }
      }

      EV_SET(&change, sigId, EVFILT_SIGNAL, EV_ADD | EV_ENABLE, 0, NULL, NULL);
      if (kevent(m_kqueue, &change, 1, NULL, 0, NULL) < 0)
      {
        gLog.LogError("Failed to add signal %s (%d) handler to kqueue: %s", strsignal(sigId), sigId, strerror(errno));
        return false;
      }


      schedulerSignalCallback callbackData;
      callbackData.callback = callback;
      callbackData.userdata = userdata;
      m_signalCallbacks[sigId] = callbackData;

      return true;
    }

    void RemoveSignalCallback(int sigId, SignalCallback callback, bool restoreDefault) 
    {
      struct kevent change;

      signal_hash_map::iterator foundIt;

      LogAssert(IsMainThread());
      LogAssert(sigId > 0);
      LogAssert(m_kqueue!=-1);

      // See if there is anything to remove
      foundIt = m_signalCallbacks.find(sigId);
      if ( foundIt == m_signalCallbacks.end())
      {
        LogAssertFalse("RemoveSignalCallback called but there is no callback.");
        return;
      }

      if (foundIt->second.callback != callback)
      {
        LogAssertFalse("RemoveSignalCallback called with incorrect callback.");
        return;
      }

      //
      // its a match
      // 
      if (restoreDefault)
      {
        if (SIG_ERR == ::signal(sigId, SIG_DFL))
        {
          gLog.LogError("Failed to restore signal %s (%d) deleting handler from kqueue: %s", strsignal(sigId), sigId, strerror(errno));
          LogAssertFalse("SIG_DFL");
        }
        // continue anyway.
      }


      EV_SET(&change, sigId, EVFILT_SIGNAL, EV_DELETE, 0, NULL, NULL);
      if (kevent(m_kqueue, &change, 1, NULL, 0, NULL) < 0)
        gLog.LogError("Failed to remove signal %s (%d) handler from kqueue: %s", strsignal(sigId), sigId, strerror(errno));

      m_signalCallbacks.erase(foundIt);
    }

    void RequestShutdown()
    {
      LogAssert(IsMainThread());
      m_wantsShutdown = true;
    }

    Timer *MakeTimer(const char *name)
    {
      m_timerCount++;
      return new TimerImpl(*this, &m_activeTimers, name);
    }

    /**
     * Call when completely done with a timer.
     * 
     * @param timer 
     */
    void FreeTimer(Timer *timer)
    {
      if (timer)
      {
        TimerImpl *theTimer = static_cast<TimerImpl *>(timer);

        m_timerCount--;
        delete theTimer;
      }

    }

    // Return true if lhs should be before rhs.
    static bool compareTimers(const TimerImpl * lhs, const TimerImpl * rhs)
    {
      // Stopped timers should not be in the list.
      LogAssert(!lhs->IsStopped() && !rhs->IsStopped());


      // We want the "earliest" timers first. So we return true if lhs is earlier than
      // rhs. -1 if left is earlier.
      return(0 > timespecCompare(lhs->GetExpireTime(), rhs->GetExpireTime()));
    }

  protected:

    //
    //
    // TimerImpl must be a contained class to avoid circular definitions.
    // It is a bit kludgey.
    //
    //

    class TimerImpl : public Timer
    {
    private:
      SchedulerImpl *m_scheduler;
      SchedulerImpl::timer_set *m_activeTimers;  // Use only from main thread. The active timer list that this is part of when active.
      Timer::Callback m_callback;
      void *m_userdata;
      struct timespec m_expireTime;
      struct timespec m_startTime;  // only valid when not stopped.
      bool m_stopped;
      char *m_name;  // for logging
      Timer::Priority::Value m_priority;


    public:
      TimerImpl(SchedulerImpl &scheduler, SchedulerImpl::timer_set *timerSet, const char *name) : Timer(),
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
          char *tempName= (char *)malloc(32); 
          snprintf(tempName, 32, "%p", this);
          m_name = tempName;
        }

        m_expireTime.tv_sec = 0;
        m_expireTime.tv_nsec = 0;
      };

      virtual ~TimerImpl() 
      {
        // This will remove it from the active list.
        Stop();
        if (m_name)
          free(m_name);
      };


      void SetCallback( Timer::Callback callback, void *userdata)
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
          timer_set_it found = m_activeTimers->find(this);
          if (found != m_activeTimers->end())
            m_activeTimers->erase(found);

          m_stopped = true;
          LogOptional(Log::TimerDetail, "Stopping timer %s. (%zu timers)", m_name, m_activeTimers->size());
        }
      }



      bool SetMsTimer(uint32_t ms)
      {
        // no check for overflow. But that would be a lot of years ;-)
        return SetMicroTimer(uint64_t(ms)*1000);
      }


      bool SetMicroTimer(uint64_t micro)
      {
        LogAssert(m_scheduler->IsMainThread());

        struct timespec startTime;

        if (!GetMonolithicTime(startTime))
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
       * @return const struct timespec& 
       */
      const struct timespec& GetExpireTime() const
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
        struct timespec expireTime;

        expireTime = startTime;
        timespecAddMicro(expireTime, micro);

        startChange = (startTime.tv_sec !=  m_startTime.tv_sec || startTime.tv_nsec !=  m_startTime.tv_nsec);
        expireChange = (m_stopped || (expireTime.tv_sec !=  m_expireTime.tv_sec || expireTime.tv_nsec !=  m_expireTime.tv_nsec));

        //LogOptional(Log::Temp, "Timer %s before change %zu items",m_name, m_activeTimers->size());

        if (!expireChange && !startChange)
        {
          LogOptional(Log::TimerDetail, "Timer %s no change.  %"PRIu64"  microseconds. Expires:%jd:%09ld", m_name, micro, (intmax_t)expireTime.tv_sec, expireTime.tv_nsec );
          return true;
        }

        LogOptional(Log::TimerDetail, "%s timer %s for %"PRIu64" microseconds from %jd:%09ld. Expires:%jd:%09ld", 
                    m_stopped ? "Starting": startChange ? "Resetting":"Advancing", 
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
          timer_set_it found = m_activeTimers->find(this);
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
      bool willTimerBeSorted(const timer_set_it item, const struct timespec &expireTime)
      {
        timer_set_it temp;

        if (item != m_activeTimers->begin())
        {
          // We want the timer to be later than, or equal to, the previous item
          if (0 > timespecCompare(expireTime, (*(--(temp=item)))->GetExpireTime()))
            return false;
        }

        (temp=item)++;
        if (temp != m_activeTimers->end())
        {
          // we want the timer to be earlier or equal to the next item.
          if (0 < timespecCompare(expireTime, (*temp)->GetExpireTime()))
            return false;
        }
        return true;
      }

    };
  };


  Scheduler *Scheduler::MakeScheduler()
  {
    return new SchedulerImpl;
  }


  void Scheduler::FreeScheduler(Scheduler *scheduler)
  {
    delete scheduler;
  }

}








