/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**

   Handles a single thread scheduling scheme.

 */
#pragma once



/**
 * This timer class is used with the Scheduler class. Most of the functions must
 * be called on the "main thread" which refers to the Scheduler main thread. See
 * Scheduler::IsMainThread()
 *
 * The timers are "one-shot" and will be put in the stopped state before the
 * callback is called if they expire.
 */
class Timer
{
public:

  /**
   * Callback for timers.
   * Always called on the main thread.
   *
   * @param [in] -  The timer that initiated the callback.
   * @param [in] - The userdata that the callback was added with.
   *
   */
  typedef void (*Callback)(Timer *timer, void *userdata);

  /**
   * Sets the callback that will execute when the timer expires.
   *
   * @note - Call only from main thread.
   *
   * @param callback [in] - The callback to call.
   * @param userdata [in]- Passed to callback.
   */
  virtual void SetCallback(Timer::Callback callback, void *userdata) = 0;

  /**
   *
   * Timer will no longer be used, until it is set again.
   *
   * @note Call only on main thread. See Scheduler::IsMainThread().
   *
   */
  virtual void Stop() = 0;

  /**
   * Is the timer currently stopped.
   * @note Call only on main thread. See Scheduler::IsMainThread().
   *
   * @return bool
   */
  virtual bool IsStopped() const = 0;

  /**
   * Sets the timer to expire (approximately) a relative time from now. The time
   * is always a one-shot timer.
   *
   * If a 0 time timer is set, then the callback will be called at the next
   * opportunity.
   *
   * @note Call only on main thread. See Scheduler::IsMainThread().
   *
   * @param ms [in] - Time from now for timer to go off in milliseconds.
   *
   * @return bool - false on failure.
   */
  virtual bool SetMsTimer(uint32_t ms) = 0;

  /**
   * Sets the timer to expire (approximately) a relative time from now. The time
   * is always a one-shot timer.
   *
   * If a 0 time timer is set, then the callback will be called at the next
   * opportunity.
   *
   * @note Call only on main thread. See Scheduler::IsMainThread().
   *
   * @param micro [in] - Time from now for timer to go off in microseconds.
   *
   * @return bool - false on failure.
   */
  virtual bool SetMicroTimer(uint64_t micro) = 0;


  /**
   * Changes the relative time to expire from the time that the timer was last
   * set. If the timer is stoped, then it will be set for a relative time from
   * now.
   *
   * The time is always a one-shot timer.
   *
   * If the time has passed, then the callback will be called at the next
   * opportunity.
   *
   * @note Call only on main thread. See Scheduler::IsMainThread().
   *
   * @param micro [in] - Time from last set for timer to go off in microseconds.
   *
   * @return bool - false on failure.
   */
  virtual bool UpdateMicroTimer(uint64_t micro) = 0;

  struct Priority
  {enum Value
    {Low = 0, Hi};};

  /**
   * Set the priority for the timer. Low priority timers only get called after all
   * incoming  packets are processed.
   *
   * @note Call only on main thread. See Scheduler::IsMainThread().
   *
   * @param pri
   */
  virtual  void SetPriority(Timer::Priority::Value pri) = 0;

  /**
   * See SetPriority().
   *
   * @note Call only on main thread. See Scheduler::IsMainThread().
   *
   * @return Timer::Priority::Value
   */
  virtual Timer::Priority::Value GetPriority() = 0;



protected:
  Timer() { };
  virtual ~Timer() { };
};


/**
 * A single thread based scheduler.
 *
 */
class Scheduler
{
public:

  virtual ~Scheduler() { };

  /**
   *
   * Starts scheduling and handling events until it is stopped.
   *
   * @note Call only on main thread. See IsMainThread().
   *
   * @return - false if failed to start up.
   */
  virtual bool Run() = 0;

  /**
   * This checks if the current thread is the one under which the scheduler was
   * created. Many functions must be called from the main thread.
   *
   * @return bool - True if the current thread is the main thread
   */
  virtual bool IsMainThread() = 0;

  typedef void (*SocketCallback)(int socket, void *userdata);

  /**
   * Will listen on this socket (or pipe), and call the callback when there is
   * data to receive. The call will occur on the main thread.
   *
   * @note Call only on main thread. See IsMainThread().
   *
   * @throw - std:bad_alloc on failure.
   *
   * @param socket [in] - The socket to listen on.
   * @param callback [in]- The callback.
   * @param userdata [in]- passed back to callback.
   *
   * @return bool
   */
  virtual bool SetSocketCallback(int socket, SocketCallback callback, void *userdata) = 0;

  /**
   * Will stop listening on the socket (or pipe).
   *
   * @note Call only on main thread. See IsMainThread().
   *
   * @param socket [in] - The socket to stop listening on..
   *
   */
  virtual void RemoveSocketCallback(int socket) = 0;

  typedef void (*SignalCallback)(int sigId, void *userdata);

  /**
   * Sets up a "signal channel" that can be used to have the callback called on
   * the main thread by calling Signal() from any thread.
   *
   * @throw - std:bad_alloc on failure.
   *
   * @note Call only on main thread. See IsMainThread().
   *
   * @param outSigId [out] - On success, this is set to the int to use with
   *                 Signal. On failure set to -1 (invalid)
   * @param callback [in] - Routine to run on signal.
   * @param userdata  [in] - Sent to callback when signaled.
   *
   * @return bool - false on failure.
   */
  virtual bool CreateSignalChannel(int *outSigId, SignalCallback callback, void *userdata) = 0;

  /**
   *
   * Signals a channel to run its callback on the main thread.
   *
   * @note May be called from ANY thread.
   *
   * @param sigId
   *
   * @return bool
   */
  virtual bool Signal(int sigId) = 0;

  /**
   * Call when done with a signal channel created with CreateSignalChannel().
   *
   * @note Call only on main thread. See IsMainThread().
   *
   * @param sigId
   */
  virtual void RemoveSignalChannel(int sigId) = 0;


  /**
   *
   * Causes the Scheduler to abandon all operations and exit the Run() loop. This
   * will happen some time after control returns to the Run() loop.
   *
   * @note Call only on main thread. See IsMainThread().
   *
   */
  virtual void RequestShutdown() = 0;

  /**
   *
   * Call to get a brand new timer that is attached to the scheduler.
   * Any remaining timer will be destroyed when the scheduler is destroyed. So be
   * careful.
   *
   * @note Call only on main thread. See IsMainThread().
   *
   * @throw - std:bad_alloc on failure.
   *
   * @param name [in] - The optional logging name for the timer.
   *
   *
   * @return Timer* - Never NULL.
   */
  virtual Timer* MakeTimer(const char *name) = 0;

  /**
   * Call when completely done with a timer.
   *
   * @note Call only on main thread. See IsMainThread().
   *
   * @param timer
   */
  virtual void FreeTimer(Timer *timer) = 0;

protected:

  /**
   * Constructor
   * The thread that calls this is considered the "main thread". See
   * Scheduler::IsMainThread().
   */
  Scheduler() { };
};
