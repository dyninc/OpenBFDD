/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Various threading and lock routines
#include "common.h"
#include "threads.h"
#include "utils.h"
#include <errno.h>

using namespace std;


/**
 * @param create - If true then the lock is created and initialized.
 */
QuickLock::QuickLock(bool create /*false*/) :
   m_initialized(false)
{
  if (create)
    Create();
}

QuickLock::~QuickLock()
{
  Destroy();
}

bool QuickLock::Create()
{
  if (!LogVerify(!m_initialized))
    return false;

  if (pthread_mutex_init(&m_Lock, NULL))
  {
    LogAssertFalse("pthread_mutex_init failed");
    return false;
  }
  m_initialized = true;
  return true;
}

void QuickLock::Destroy()
{
  if (m_initialized)
  {
    LogVerify(!pthread_mutex_destroy(&m_Lock));
    m_initialized = false;
  }
}

bool QuickLock::Lock()
{
  if (!LogVerify(m_initialized))
    return false;

  if (!LogVerify(!pthread_mutex_lock(&m_Lock)))
    return false;

  return true;
}

bool QuickLock::UnLock()
{
  if (!LogVerify(m_initialized))
    return false;

  if (!LogVerify(!pthread_mutex_unlock(&m_Lock)))
    return false;

  return true;
}


void QuickLock::SignalAndUnlock(WaitCondition &condition)
{
  if (!LogVerify(m_initialized))
    return;

  condition.Signal();

  // Unlocking after is safer, but may be more expensive. In the simple case it
  // could be unlocked before the signal. There are more complicated cases
  // involving multiple waiters with different expectations, where this could be a
  // problems. For now we err on the side of safety.
  UnLock();
}

int QuickLock::LockWait(WaitCondition &condition, uint32_t msTimeout)
{
  if (!LogVerify(m_initialized))
    return -1;
  return condition.Wait(&m_Lock, msTimeout);
}



AutoQuickLock::AutoQuickLock(QuickLock &threadLock, bool lockInitial /*true*/)
{
  m_quickLock = &threadLock;
  m_isLockedByMe = false;
  if (lockInitial)
    Lock();
}

AutoQuickLock::~AutoQuickLock()
{
  if (m_isLockedByMe)
    m_quickLock->UnLock();
}

bool AutoQuickLock::Lock()
{
  if (!LogVerify(!m_isLockedByMe))
    return true;
  // only call once
  if (!m_quickLock->Lock())
    return false;
  m_isLockedByMe = true;
  return true;
}

bool AutoQuickLock::IsLockedByMe()
{
  return m_isLockedByMe;
}

bool AutoQuickLock::UnLock()
{
  if (!LogVerify(m_isLockedByMe))
    return false;
  m_isLockedByMe = false;
  return m_quickLock->UnLock();
}

void AutoQuickLock::SignalAndUnlock(WaitCondition &condition)
{
  if (!LogVerify(m_isLockedByMe))
    return;

  m_isLockedByMe = false;
  m_quickLock->SignalAndUnlock(condition);
}

int AutoQuickLock::LockWait(WaitCondition &condition, uint32_t msTimeout)
{
  if (!LogVerify(m_isLockedByMe))
    return -1;

  return m_quickLock->LockWait(condition, msTimeout);
}


WaitCondition::WaitCondition(bool init) :
   m_initDone(false)
{
  if (init)
    Init();
}

WaitCondition::~WaitCondition()
{
  if (m_initDone)
    LogVerify(!pthread_cond_destroy(&m_condition));
}

bool WaitCondition::Init()
{
  if (m_initDone)
  {
    LogAssertFalse("WaitCondition::Init called more than once.");
    return true;
  }

  if (LogVerify(!pthread_cond_init(&m_condition, NULL)))
    m_initDone = true;

  return m_initDone;
}

void WaitCondition::Signal()
{
  if (!m_initDone)
  {
    LogAssertFalse("signaling on uninitialized signal");
    return;
  }

  LogVerify(!pthread_cond_signal(&m_condition));
}

int WaitCondition::Wait(pthread_mutex_t *lock, uint32_t msTimeout)
{
  int ret;

  if (!m_initDone)
  {
    LogAssertFalse("waiting on uninitialized signal");
    return -1;
  }

  if (msTimeout)
  {
    struct timespec waitTime;
    clock_gettime(CLOCK_REALTIME, &waitTime);
    timespecAddMs(waitTime, msTimeout);
    ret = pthread_cond_timedwait(&m_condition, lock, &waitTime);
  }
  else
    ret = pthread_cond_wait(&m_condition, lock);

  if (ret == 0)
    return 0;
  if (ret == ETIMEDOUT)
    return 1;
  else
  {
    LogVerifyFalse("pthread_cond_wait failed");
    return -1;
  }
}

/**
 * @param create - If true then the lock is created and initialized.
 */
ReadWriteLock::ReadWriteLock(bool create /*false*/) :
   m_initialized(false)
{
  if (create)
    Create();
}

ReadWriteLock::~ReadWriteLock()
{
  Destroy();
}

bool ReadWriteLock::Create()
{
  if (!LogVerify(!m_initialized))
    return false;

  if (pthread_rwlock_init(&m_Lock, NULL))
  {
    LogAssertFalse("pthread_rwlock_init failed");
    return false;
  }
  m_initialized = true;
  return true;
}

void ReadWriteLock::Destroy()
{
  if (m_initialized)
  {
    LogVerify(!pthread_rwlock_destroy(&m_Lock));
    m_initialized = false;
  }
}

bool ReadWriteLock::ReadLock()
{
  if (!LogVerify(m_initialized))
    return false;

  if (!LogVerify(!pthread_rwlock_rdlock(&m_Lock)))
    return false;

  return true;
}

bool ReadWriteLock::WriteLock()
{
  if (!LogVerify(m_initialized))
    return false;

  if (!LogVerify(!pthread_rwlock_wrlock(&m_Lock)))
    return false;

  return true;
}


bool ReadWriteLock::UnLock()
{
  if (!LogVerify(m_initialized))
    return false;

  if (!LogVerify(!pthread_rwlock_unlock(&m_Lock)))
    return false;

  return true;
}


AutoReadWriteLock::AutoReadWriteLock(ReadWriteLock &threadLock, AutoReadWriteLock::LockType lockInitial)
{
  m_rwLock = &threadLock;
  m_lockedByMeType = AutoReadWriteLock::None;
  if (lockInitial == AutoReadWriteLock::Read)
    ReadLock();
  else if (lockInitial == AutoReadWriteLock::Write)
    WriteLock();

}

AutoReadWriteLock::~AutoReadWriteLock()
{
  if (m_lockedByMeType != AutoReadWriteLock::None)
    m_rwLock->UnLock();
}

bool AutoReadWriteLock::ReadLock()
{
  if (!LogVerify(m_lockedByMeType == AutoReadWriteLock::None))
    return true;
  // only call once
  if (!m_rwLock->ReadLock())
    return false;
  m_lockedByMeType = AutoReadWriteLock::Read;
  return true;
}

bool AutoReadWriteLock::WriteLock()
{
  if (!LogVerify(m_lockedByMeType == AutoReadWriteLock::None))
    return true;
  // only call once
  if (!m_rwLock->WriteLock())
    return false;
  m_lockedByMeType = AutoReadWriteLock::Write;
  return true;
}


bool AutoReadWriteLock::IsLockedByMe()
{
  return (m_lockedByMeType != AutoReadWriteLock::None);
}

bool AutoReadWriteLock::UnLock()
{
  if (!LogVerify(m_lockedByMeType != AutoReadWriteLock::None))
    return false;
  m_lockedByMeType = AutoReadWriteLock::None;
  return m_rwLock->UnLock();
}
