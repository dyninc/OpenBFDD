/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "config.h"
#ifdef USE_KEVENT_SCHEDULER

#include "common.h"
#include "KeventScheduler.h"
#include "utils.h"
#include <errno.h>

using namespace std;


KeventScheduler::KeventScheduler() : SchedulerBase(),
   m_totalEvents(0),
   m_foundEvents(0),
   m_nextCheckEvent(0),
   m_events(m_totalEvents + 1)
{
  m_kqueue = ::kqueue();
  if (m_kqueue < 0)
    gLog.Message(Log::Critical, "Failed to create Scheduler kqueue. Can not proceed.");
}

KeventScheduler::~KeventScheduler()
{
}


bool KeventScheduler::waitForEvents(const struct timespec &timeout)
{
  m_nextCheckEvent = 0;

  m_foundEvents = ::kevent(m_kqueue, NULL, 0, &m_events.front(), m_events.size(), &timeout);
  if (m_foundEvents < 0)
  {
    m_foundEvents = 0;
    gLog.LogError("kevent failed: %s", ErrnoToString());
  }
  else if (m_foundEvents == 0)
  {
    if (timeout.tv_nsec != 0 || timeout.tv_sec != 0)
      gLog.Optional(Log::TimerDetail, "kevent timeout");
  }
  else
    gLog.Optional(Log::TimerDetail, "kevent received %d events", m_foundEvents);

  return m_foundEvents > 0;
}

int KeventScheduler::getNextSocketEvent()
{
  if (!LogVerify(m_foundEvents <= int(m_events.size())))
    m_foundEvents = m_events.size();


  for (; m_nextCheckEvent < m_foundEvents; m_nextCheckEvent++)
  {
    // TODO check for EV_ERROR in flags?
    if (m_events[m_nextCheckEvent].filter == EVFILT_READ)
    {
      // We have a socket event
      return m_events[m_nextCheckEvent++].ident;
    }
    else
    {
      // We should only have socket events
      gLog.LogError("Unexpected kevent event %" PRIuPTR " got result of %hu",
                    m_events[m_nextCheckEvent].ident,
                    m_events[m_nextCheckEvent].filter);
    }
  }

  return -1;
}

bool KeventScheduler::watchSocket(int fd)
{
  struct kevent change;

  if (!LogVerify(m_kqueue != -1))
    return false;

  EV_SET(&change, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
  if (kevent(m_kqueue, &change, 1, NULL, 0, NULL) < 0)
  {
    gLog.ErrnoError(errno, "Failed to add socket to kqueue");
    return false;
  }

  m_totalEvents++;
  resizeEvents();

  return true;
}

void KeventScheduler::unWatchSocket(int fd)
{
  struct kevent change;

  LogAssert(m_kqueue != -1);

  EV_SET(&change, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  if (kevent(m_kqueue, &change, 1, NULL, 0, NULL) < 0)
    gLog.ErrnoError(errno, "Failed to remove socket to kqueue");
  else
  {
    if (m_totalEvents > 0)
      m_totalEvents--;
    resizeEvents();
  }
}

/**
 * resizes m_events.
 *
 * @throw - May throw.
 *
 */
void KeventScheduler::resizeEvents()
{
  // Can not resize if we may be using it.
  if (m_totalEvents < m_foundEvents)
    return;

  m_events.resize(m_totalEvents + 1);
}


#endif  // USE_KEVENT_SCHEDULER
