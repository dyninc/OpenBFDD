/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Various threading and lock routines
#pragma once

#include <pthread.h>


/**
 * Helper/wrapper for pthread_cond_t Could handle initialization failure better.
 */
class WaitCondition
{
public:
  WaitCondition(bool init = true);
  ~WaitCondition();

  /**
   * Signals the condition.
   */
  void Signal();

  /**
   * Only call if this is not created with init == true;
   *
   * @return bool - false on failure
   */
  bool Init();

  /**
   * Waits on the signal and lock. Note that spurious wakeups can occur.
   * On failure other than timeout the mutex and signal state will not have
   * changed.
   *
   * @param lock [in]- Must not be NULL.
   * @param msTimeout [in] - The timeout time, or 0 for no timeout.
   *
   * @return int - 0 on success. 1 on timeout, -1 on other failure.
   */
  int Wait(pthread_mutex_t *lock, uint32_t msTimeout = 0);


private:
  pthread_cond_t m_condition;
  bool m_initDone;  // the condition initialization succeeded.
};


/**
 * Wrapper for a simple mutex.
 */
class QuickLock
{
public:
  /**
   * @param create - If true then the lock is created and initialized.
   */
  QuickLock(bool create = false);
  ~QuickLock();

  /**
   * Use to create the lock, if constructor was not called with create.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool Create();

  /**
   * Permanently destroys the lock. Do not call any other methods after this
   * unless Create is called.
   */
  void Destroy();

  /**
   *  Locks the lock. Normally use an AutoQuickLock on the stack to ensure that
   *  this gets unlocked.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool Lock();

  /**
   *  Unlocks the lock. Normally use an AutoQuickLock on the stack to ensure that
   *  this gets unlocked.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool UnLock();


  /**
   * Signal a condition.
   * Lock must be locked by us.
   * On return, this is always unlocked. (even on failure)
   *
   * @param condition
   */
  void SignalAndUnlock(WaitCondition &condition);

  /**
   *
   * Waits on a condition. Lock must be locked by us.
   * On return, this is still locked.
   *
   * @param condition
   * @param msTimeout [in] - The timeout time, or 0 for no timeout.
   *
   * @return int - 0 on success. 1 on timeout, -1 on other failure.
   */
  int LockWait(WaitCondition &condition, uint32_t msTimeout = 0);


private:
  pthread_mutex_t m_Lock;
  bool m_initialized;
};

// Helper for QuickLock.  This object should only be used from a single thread.
class AutoQuickLock
{
public:
  /**
   *
   *
   * @param threadLock - The lock.
   * @param lockInitial - Should the lock start in the locked state. Use
   *                    IsLockedByMe to check for failure. Will log on failure.
   */
  AutoQuickLock(QuickLock &threadLock, bool lockInitial = true);
  ~AutoQuickLock();

  /**
   * Lock the lock.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool Lock();

  /**
   * Check if this object had locked the lock.
   *
   * @return bool - true if this object had locked the lock.
   */
  bool IsLockedByMe();

  /**
   *  Unlock the lock.
   *  Called automatically when this is destroyed.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool UnLock();

  /**
   * Signal a condition.
   * Lock must be locked by us.
   * On return, this is always unlocked. (even on failure)
   *
   * @param condition
   */
  void SignalAndUnlock(WaitCondition &condition);

  /**
   *
   * Waits on a condition. Lock must be locked by us.
   * On return, this is still locked.
   *
   * @param condition
   * @param msTimeout [in] - The timeout time, or 0 for no timeout.
   *
   * @return int - 0 on success. 1 on timeout, -1 on other failure.
   */
  int LockWait(WaitCondition &condition, uint32_t msTimeout = 0);



private:
  QuickLock *m_quickLock;
  bool m_isLockedByMe;
};


/**
 * Wrapper for a read/write lock.
 */
class ReadWriteLock
{
public:

  /**
   * @param create - If true then the lock is created and initialized.
   */
  ReadWriteLock(bool create = false);
  ~ReadWriteLock();

  /**
   * Use to create the lock, if constructor was not called with create.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool Create();

  /**
   * Permanently destroys the lock. Do not call any other methods after this
   * unless Create is called.
   */
  void Destroy();

  /**
   *  Shared locks the lock. Normally use an AutoReadWriteLock on the stack
   *  to ensure that this gets unlocked.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool ReadLock();

  /**
   *  Exclusive locks the lock . Normally use an AutoReadWriteLock on the stack to ensure
   *  that this gets unlocked.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool WriteLock();

  /**
   *  Unlocks the lock. Normally use an AutoRWriteLock on the stack to ensure that
   *  this gets unlocked.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool UnLock();

private:
  pthread_rwlock_t m_Lock;
  bool m_initialized;
};


// Helper for RWriteLock.  This object should only be used from a single thread.
class AutoReadWriteLock
{
public:

  enum LockType
  {
    None, // not locked
    Read, // Read locked
    Write // write locked.
  };

  /**
   *
   *
   * @param threadLock - The lock.
   * @param lockInitial - Should the lock start in the locked state. Use
   *                    IsLockedByMe to check for failure. Will log on failure.
   */
  AutoReadWriteLock(ReadWriteLock &threadLock, AutoReadWriteLock::LockType lockInitial);
  ~AutoReadWriteLock();

  /**
   * Read lock the lock.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool ReadLock();

  /**
   * Exclusive lock the lock.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool WriteLock();

  /**
   * Check if this object had locked the lock.
   *
   * @return bool - true if this object had locked the lock.
   */
  bool IsLockedByMe();

  /**
   *  Unlock the lock.
   *  Called automatically when this is destroyed.
   *
   *  @return bool - false on failure. (Will log failure.)
   */
  bool UnLock();

private:
  ReadWriteLock *m_rwLock;
  AutoReadWriteLock::LockType m_lockedByMeType;
};
