/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "config.h"
#ifndef USE_KEVENT_SCHEDULER

#include "common.h"
#include "SelectScheduler.h"
#include "utils.h"
#include <errno.h>
#include <string.h>

using namespace std;



SelectScheduler::SelectScheduler() : SchedulerBase(),
   m_foundEvents(0),
   m_nextCheckEvent(0)
{
}

SelectScheduler::~SelectScheduler()
{
}


bool SelectScheduler::waitForEvents(const struct timespec &timeout)
{
  fd_set watchSet;
  struct timeval tv;
  int largestSocket = 0;
  set<int>::iterator it;

  timespecToTimeval(timeout,  tv);
  m_nextCheckEvent = 0;

  /**
   * Make the set of sockets to watch.
   */
  FD_ZERO(&watchSet);
  for (it = m_watchSockets.begin(); it != m_watchSockets.end(); it++)
  {
    FD_SET(*it, &watchSet);
    largestSocket = max(largestSocket,  *it);
  }

  m_foundEvents = ::select(largestSocket + 1, &watchSet, NULL, NULL, &tv);

  if (m_foundEvents < 0)
  {
    m_foundEvents = 0;
    gLog.LogError("select failed: %s", ErrnoToString());
  }
  else if (m_foundEvents == 0)
  {
    if (timeout.tv_nsec != 0 || timeout.tv_sec != 0)
      gLog.Optional(Log::TimerDetail, "select timeout");
  }
  else
  {
    int actuallyFound = 0;

    gLog.Optional(Log::TimerDetail, "select received %d events", m_foundEvents);

    // Determine what the sockets that hit are
    for (it = m_watchSockets.begin(); it != m_watchSockets.end(); it++)
    {
      if (FD_ISSET(*it, &watchSet))
      {
        if (!LogVerify(actuallyFound < (int)m_foundSockets.size()))
          break;
        m_foundSockets[actuallyFound++] = *it;
      }
    }

    LogVerify(actuallyFound == m_foundEvents);
    m_foundEvents = actuallyFound;
  }

  return m_foundEvents > 0;
}

int SelectScheduler::getNextSocketEvent()
{
  if (!LogVerify(m_foundEvents <= int(m_foundSockets.size())))
    m_foundEvents = m_foundSockets.size();

  if (m_nextCheckEvent < m_foundEvents)
    return m_foundSockets[m_nextCheckEvent++];
  else
    return -1;
}

bool SelectScheduler::watchSocket(int fd)
{
  if (!LogVerify(fd != -1))
    return false;

  m_watchSockets.insert(fd);
  resizeFoundSockets();

  return true;
}

void SelectScheduler::unWatchSocket(int fd)
{
  if (!LogVerify(fd != -1))
    return;

  LogVerify(1 == m_watchSockets.erase(fd));
  resizeFoundSockets();
}

/**
 * resizes m_foundSockets.
 *
 * Note that m_foundSockets is maintained a vector big enough to hold all
 * events so that resizing would occur only when adding or removing sockets.
 * This prevents exceptions being thrown during regular operation.
 *
 * @throw - May throw.
 *
 */
void SelectScheduler::resizeFoundSockets()
{
  // Can not resize if we may be using it.
  if ((int)m_watchSockets.size() < m_foundEvents)
    return;

  m_foundSockets.resize(m_watchSockets.size());
}

#endif  // !USE_KEVENT_SCHEDULER
