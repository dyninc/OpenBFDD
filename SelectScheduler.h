/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**

  Scheduler implementation for system with select, but without kevent..

 */
#pragma once

#include "config.h"

#ifndef USE_KEVENT_SCHEDULER

#include "SchedulerBase.h"
#include <vector>
#include <set>

class SelectScheduler : public SchedulerBase
{

public:
public:
  /**
   * Constructor
   * The thread that calls this is considered the "main thread". See
   * Scheduler::IsMainThread().
   */
  SelectScheduler();
  virtual ~SelectScheduler();

protected:

  /** Overrides from  SchedulerBase  */
  virtual bool watchSocket(int fd);
  virtual void unWatchSocket(int fd);
  virtual bool waitForEvents(const struct timespec &timeout);
  virtual int getNextSocketEvent();


private:

  void resizeFoundSockets();

  int m_foundEvents;  // from last waitForEvents()
  std::vector<int> m_foundSockets; // from last waitForEvents().
  int m_nextCheckEvent;  // for getNextSocketEvent
  std::set<int> m_watchSockets;
};


#endif  // !USE_KEVENT_SCHEDULER
