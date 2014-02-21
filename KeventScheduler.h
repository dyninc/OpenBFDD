/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**

  Scheduler implementation for system with kevent.

 */
#pragma once

#include "config.h"

#ifdef USE_KEVENT_SCHEDULER

#include "SchedulerBase.h"
#include <sys/event.h>
#include <vector>

class KeventScheduler : public SchedulerBase
{

public:
  /**
   * Constructor
   * The thread that calls this is considered the "main thread". See
   * Scheduler::IsMainThread().
   */
  KeventScheduler();
  virtual ~KeventScheduler();

protected:

  /** Overrides from  SchedulerBase  */
  virtual bool watchSocket(int fd);
  virtual void unWatchSocket(int fd);
  virtual bool waitForEvents(const struct timespec &timeout);
  virtual int getNextSocketEvent();


private:

  void resizeEvents();

  int m_totalEvents;
  int m_kqueue;
  int m_foundEvents; // from last waitForEvents()
  int m_nextCheckEvent;  // for getNextSocketEvent
  std::vector<struct kevent> m_events; // from last waitForEvents()
};


#endif  // USE_KEVENT_SCHEDULER
