/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**

  Base class for implementations of the Scheduler interface.

 */
#pragma once

#include "Scheduler.h"
#include "TimeSpec.h"
#include "hash_map.h"
#include <set>

struct timespec;

class TimerImpl;

class SchedulerBase : public Scheduler
{
public:
  typedef  std::multiset<TimerImpl *, bool (*)(const TimerImpl *, const TimerImpl *)> timer_set;
  typedef  timer_set::iterator timer_set_it;

public:
  virtual ~SchedulerBase();

  /** Scheduler interface impl */
  virtual bool Run();
  virtual bool IsMainThread();
  virtual bool SetSocketCallback(int socket, Scheduler::SocketCallback callback, void *userdata);
  virtual void RemoveSocketCallback(int socket);
  virtual bool CreateSignalChannel(int *outSigId, SignalCallback callback, void *userdata);
  virtual bool Signal(int sigId);
  virtual void RemoveSignalChannel(int sigId);
  virtual void RequestShutdown();
  virtual Timer* MakeTimer(const char *name);
  virtual void FreeTimer(Timer *timer);

  /** Other public functions */
  static timer_set_it TimeSetFindExact(timer_set &timerSet, TimerImpl *target);


protected:
  /**
   * Constructor
   * The thread that calls this is considered the "main thread". See
   * Scheduler::IsMainThread().
   */
  SchedulerBase();

  /**
   *
   * Called to add a socket or  pipe to the list of watched events.
   *
   * @note Called only on main thread. See Scheduler::IsMainThread().
   *
   * @param fd
   *
   * @return bool - false on failure
   */
  virtual bool watchSocket(int fd) = 0;

  /**
   *
   * Called to remove a socket or  pipe to the list of watched events.
   *
   * @note Called only on main thread. See Scheduler::IsMainThread().
   *
   * @param fd
   *
   */
  virtual void unWatchSocket(int fd) = 0;


  /**
   * Called to wait for events.
   *
   * Subsequent calls to getNextSocketEvent() are based on this call.
   *
   * @note Called only on main thread. See Scheduler::IsMainThread().
   *
   * @param timeout - The maximum time to wait.
   *
   * @return bool - True if there was an actual socket event.
   */
  virtual bool waitForEvents(const struct timespec &timeout) = 0;

  /**
   * Iterate over socket events found in last call to waitForEvents. Each
   * subsequent call should return the next socket event (in any order).
   *
   * @note Called only on main thread. See Scheduler::IsMainThread().
   *
   * @return int - The next socket that was found in waitForEvents. -1 if there
   *         are no more sockets.
   */
  virtual int getNextSocketEvent() = 0;

private:

  TimeSpec getNextTimerTimeout();
  bool expireTimer(Timer::Priority::Value minPri);
  static bool compareTimers(const TimerImpl *lhs, const TimerImpl *rhs);

  pthread_t m_mainThread; // This is the thread under which Run was called.
                          //
                          // Only access these from the Main scheduler thread. See IsMainThread().
                          //
  bool m_isStarted; // True if Run was called.

  struct schedulerSignalItem
  {
    Scheduler::SignalCallback callback;
    void *userdata;
    int fdWrite;  // write end of pipe  (also the signalId)
    int fdRead; // read end of pipe
  };

  typedef  hash_map<int, schedulerSignalItem>::Type SignalItemHashMap;

  struct schedulerSocketItem
  {
    Scheduler::SocketCallback callback;
    void *userdata;
    int socket;
  };

  typedef  hash_map<int, schedulerSocketItem>::Type SocketItemHashMap;

  SocketItemHashMap m_sockets;
  SignalItemHashMap m_signals;

  bool m_wantsShutdown;
  timer_set m_activeTimers;
  int m_timerCount;   // only used for debugging
};
