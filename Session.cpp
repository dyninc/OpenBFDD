/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "Session.h"
#include "utils.h"
#include "Beacon.h"
#include "Scheduler.h"
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

using namespace std;


#ifdef TEST_DROP_FINAL
#warning TEST_DROP_FINAL defined ... testing only!
static int gDropFinalPercent = 60;
#else
static int gDropFinalPercent = 0;
#endif

static const size_t MaxUptimeCount = 4; // number of states to keep track of for logging.

uint32_t Session::m_nextId = 1;

Session::InitialParams::InitialParams() :
   detectMulti(3),
   desiredMinTx(bfd::BaseMinTxInterval),
   requiredMinRx(1000000),
   controlPlaneIndependent(false),
   adminUpPollWorkaround(true)
{
}

// Note that the inclusion of the Beacon pointer is bad design and sheer
// laziness. This should really be an event sink or callback.
Session::Session(Scheduler &scheduler, Beacon *beacon, uint32_t descriminator, const InitialParams &params) :
   m_beacon(beacon),
   m_scheduler(&scheduler),
   m_remoteAddr(),
   m_remoteSourcePort(0),
   m_localAddr(),
   m_sendPort(0),
   m_isActive(false),
   m_sessionState(bfd::State::Down),
   m_remoteSessionState(bfd::State::Down),
   m_localDiscr(descriminator),
   m_remoteDiscr(0),
   m_localDiag(bfd::Diag::None),
   m_desiredMinTxInterval(bfd::BaseMinTxInterval), // Since we start "down" this must be 1s see v10/6.8.3
   m_requiredMinRxInterval(params.requiredMinRx),
   m_remoteMinRxInterval(1),
   m_demandMode(false),  // We get to choose this.
   m_remoteDemandMode(false),
   m_detectMult(params.detectMulti),
   m_authType(bfd::AuthType::None), // ??
   m_rcvAuthSeq(0),
   m_xmitAuthSeq(rand() % UINT32_MAX),
   m_authSeqKnown(false),
   m_pollState(PollState::None),
   m_pollReceived(false),
   m_remoteDetectMult(0),
   m_remoteDesiredMinTxInterval(0),
   m_remoteDiag(bfd::Diag::None),
   m_destroyAfterTimeouts(3),
   m_remoteDestroyAfterTimeouts(3),
   m_timeoutStatus(TimeoutStatus::None),
   m_isSuspended(false),
   m_immediateControlPacket(false),
   m_controlPlaneIndependent(params.controlPlaneIndependent),
   m_adminUpPollWorkaround(params.adminUpPollWorkaround),
   m_forcedState(false),
   m_wantsPollForNewDesiredMinTxInterval(false),
   _useDesiredMinTxInterval(bfd::BaseMinTxInterval),  // Since we start "down" this must be 1s see v10/6.8.3
   m_defaultDesiredMinTxInterval(params.desiredMinTx),  // this will not take effect until we are up ... see m_useDesiredMinTxInterva
   m_wantsPollForNewRequiredMinRxInterval(false),
   _useRequiredMinRxInterval(m_requiredMinRxInterval),
   m_receiveTimeoutTimer(this),
   m_transmitNextTimer(this)
{
  LogAssert(m_scheduler->IsMainThread());

  if (m_nextId == 0)
  {
    // This is unlikely, since we can handle 4 billion sessions.
    gLog.LogError("Maximum session count exceeded, refusing new sessions.");
  }
  else
    m_id = m_nextId;

  // It may be more efficient to wait until we need these?
  char name[32];
  snprintf(name, sizeof(name), "<Rcv %u>", m_id);
  m_receiveTimeoutTimer = m_scheduler->MakeTimer(name);
  snprintf(name, sizeof(name), "<Tx %u>", m_id);
  m_transmitNextTimer = m_scheduler->MakeTimer(name);

  m_receiveTimeoutTimer->SetCallback(handleReceiveTimeoutTimerCallback,  this);
  m_receiveTimeoutTimer->SetPriority(Timer::Priority::Low);
  m_transmitNextTimer->SetCallback(handletTransmitNextTimerCallback,  this);
  m_transmitNextTimer->SetPriority(Timer::Priority::Hi);

  logSessionTransition();

  // Update this only at the end, in case an exception is thrown.
  m_nextId++;
}

Session::~Session()
{
  LogAssert(m_scheduler->IsMainThread());
}


void Session::deleteTimer(Timer *timer)
{
  LogAssert(m_scheduler->IsMainThread());
  if (m_scheduler && timer)
  {
    gLog.Message(Log::Temp,  "Free timer %p", timer);
    m_scheduler->FreeTimer(timer);
  }
}

bool Session::StartPassiveSession(const SockAddr &remoteAddr, const IpAddr &localAddr)
{
  LogAssert(m_scheduler->IsMainThread());

  // this should only be called once.
  LogAssert(!m_remoteAddr.IsValid());
  LogAssert(!m_localAddr.IsValid());
  if (!LogVerify(remoteAddr.HasPort()))
    return false;
  if (!LogVerify(!localAddr.IsAny()))
    return false;

  m_remoteAddr = IpAddr(remoteAddr);
  m_remoteSourcePort = remoteAddr.Port();

  m_localAddr = localAddr;
  m_isActive = false;
  return true;
}



bool Session::StartActiveSession(const IpAddr &remoteAddr, const IpAddr &localAddr)
{
  LogAssert(m_scheduler->IsMainThread());

  // this should only be called once.
  if (!LogVerify(!m_remoteAddr.IsValid()))
    return false;
  if (!LogVerify(!m_localAddr.IsValid()))
    return false;
  // Any is not valid send address
  if (!LogVerify(!localAddr.IsAny()))
    return false;

  m_remoteAddr = remoteAddr;
  m_remoteSourcePort = 0;

  m_localAddr = localAddr;
  m_isActive = true;

  // Start the timers now, and begin sending connection packets.
  // We set m_immediateControlPacket because this is a new connection, and there
  // is no point in waiting.
  m_immediateControlPacket = true;
  scheduleTransmit();
  return true;
}

bool Session::UpgradeToActiveSession()
{
  LogAssert(m_scheduler->IsMainThread());

  // Must already have a passive session.
  if (!LogVerify(m_remoteAddr.IsValid()))
    return false;
  if (!LogVerify(m_localAddr.IsValid()))
    return false;
  if (!LogVerify(!IsActiveSession()))
    return false;

  m_isActive = true;

  // Start the timers now, and begin sending connection packets
  scheduleTransmit();
  return true;
}


const IpAddr& Session::GetRemoteAddress()
{
  LogAssert(m_scheduler->IsMainThread());
  return m_remoteAddr;
}

const IpAddr& Session::GetLocalAddress()
{
  LogAssert(m_scheduler->IsMainThread());
  return m_localAddr;
}

bool Session::IsActiveSession()
{
  LogAssert(m_scheduler->IsMainThread());
  return m_isActive;
}


// static
bool Session::InitialProcessControlPacket(const uint8_t *data, size_t dataLength, BfdPacket &outPacket)
{
  BfdPacketHeader &header = outPacket.header;

  if (dataLength < bfd::BasePacketSize)
  {
    gLog.Optional(Log::Discard, "Discard packet: too small %zu", dataLength);
    return false;
  }

  memcpy(&outPacket, data, min(dataLength, sizeof(outPacket)));

  // the early stuff can be checked without worrying about byte order, since they
  // are parsed as bytes.
  if (header.GetVersion() != 0 && header.GetVersion() != 1)
  {
    gLog.Optional(Log::Discard, "Discard packet: bad version %hhu", header.GetVersion());
    return false;
  }

  if (header.GetAuth())
  {
    if (header.length < bfd::BasePacketSize + bfd::AuthHeaderSize)
    {
      gLog.Optional(Log::Discard, "Discard packet: length too small to include auth %hhu", header.length);
      return false;
    }
  }
  else if (header.length < bfd::BasePacketSize)
  {
    gLog.Optional(Log::Discard, "Discard packet: length too small %hhu", header.length);
    return false;
  }
  else if (header.length > dataLength)
  {
    gLog.Optional(Log::Discard, "Discard packet: length larger than data %hhu", header.length);
    return false;
  }

  if (header.detectMult == 0)
  {
    gLog.Optional(Log::Discard, "Discard packet: detectMult is 0.");
    return false;
  }

  if (header.GetMultipoint())
  {
    gLog.Optional(Log::Discard, "Discard packet: Multipoint bit is set.");
    return false;
  }

  // Can check for 0 without byte order concerns.
  if (header.myDisc == 0)
  {
    gLog.Optional(Log::Discard, "Discard packet: Source Discriminator is 0.");
    return false;
  }

  // Can check for 0 without byte order concerns.
  if (header.yourDisc == 0 && header.GetState() != bfd::State::Down && header.GetState() != bfd::State::AdminDown)
  {
    gLog.Optional(Log::Discard, "Discard packet: No destination discriminator and state is %s.", bfd::StateName(header.GetState()));
    return false;
  }

  // Now we need to worry about byte order
  header.myDisc               = ntohl(header.myDisc);
  header.yourDisc             = ntohl(header.yourDisc);
  header.txDesiredMinInt      = ntohl(header.txDesiredMinInt);
  header.rxRequiredMinInt     = ntohl(header.rxRequiredMinInt);
  header.rxRequiredMinEchoInt = ntohl(header.rxRequiredMinEchoInt);

  // packet is good as far as we can tell without a session.
  return true;
}

bool Session::ProcessControlPacket(const BfdPacket &packet, in_port_t port)
{
  // Assumes that the first few checks have been done.
  const BfdPacketHeader &header = packet.header;
  uint32_t olduseDesiredMinTxInterval = getUseDesiredMinTxInterval();
  uint32_t oldRemoteMinRxInterval = m_remoteMinRxInterval;

  LogAssert(m_scheduler->IsMainThread());

  logPacketContents(packet, false, true, m_remoteAddr, port, m_localAddr, 0);

  if (gDropFinalPercent != 0)
  {
    // For testing only
    if (header.GetFinal() && rand() % 100 < gDropFinalPercent)
    {
      gLog.Optional(Log::Discard, "Discard packet: TESTING final bit set.");
      return false;
    }
  }


  if (header.yourDisc != 0 && header.yourDisc != m_localDiscr)
  {
    gLog.Optional(Log::Discard, "Discard packet: Source Discriminator is does not match our discriminator.");
    return false;
  }

  if (header.GetAuth())
  {
    if (packet.auth.GetAuthType() == bfd::AuthType::None)
    {
      gLog.Optional(Log::Discard, "Discard packet: Auth bit set but type is None.");
      return false;
    }
    // We need to do authentication
    gLog.LogWarn("Authentication requested, but we do not handle it currently.");
    gLog.Optional(Log::Discard, "Discard packet: Auth bit set and we do not handle it.");
    return false;
  }
  else
  {
    if (m_authType != bfd::AuthType::None)
    {
      gLog.Optional(Log::Discard, "Discard packet: Auth bit clear, but session is using authentication.");
      return false;
    }
  }


  if (header.GetDemand())
  {
    {
      gLog.Optional(Log::Error, "Discard packet: We do not support demand mode for remote host.");
      return false;
    }
  }


  //
  // looks like packet can not be discarded after this point
  //

  m_remoteDesiredMinTxInterval = header.txDesiredMinInt;
  m_remoteDetectMult = header.detectMult;
  m_remoteDiscr = header.myDisc;
  m_remoteSessionState = header.GetState();
  m_remoteDemandMode = header.GetDemand();
  m_remoteMinRxInterval = header.rxRequiredMinInt;
  m_remoteDiag = header.GetDiag();

  if (header.rxRequiredMinEchoInt == 0)
  {
    // We do not handle echo anyway, but if we did, then we would stop.
  }

  if (header.GetFinal())
  {
    // Poll sequence, if any, must stop
    if (m_pollState != PollState::Polling)
      gLog.Optional(Log::Packet, "Unmatched Final bit in packet. ");
    else
      transitionPollState(PollState::Completed);
  }
  else
  {
    // Any poll sequence is now completely finished without ambiguity. (Assuming we
    // are in the Competed state now.)
    transitionPollState(PollState::None);
  }

  // Note, the spec has us checking header.GetState(), but that will always
  // (currently) be the same as m_remoteSessionState.
  LogAssert(m_remoteSessionState == header.GetState());

  if (bfd::State::AdminDown == m_remoteSessionState)
    setSessionState(bfd::State::Down, bfd::Diag::NeighborSessionDown, SetValueFlags::PreventTxReschedule);
  else
  {
    if (m_sessionState == bfd::State::Down)
    {
      if (m_remoteSessionState == bfd::State::Down)
        setSessionState(bfd::State::Init, bfd::Diag::None, SetValueFlags::PreventTxReschedule);
      else if (m_remoteSessionState == bfd::State::Init)
        setSessionState(bfd::State::Up, bfd::Diag::None, SetValueFlags::PreventTxReschedule);
    }
    else if (m_sessionState == bfd::State::Init)
    {
      if (m_remoteSessionState == bfd::State::Init
          || m_remoteSessionState == bfd::State::Up)
        setSessionState(bfd::State::Up, bfd::Diag::None, SetValueFlags::PreventTxReschedule);
    }
    else if (m_sessionState == bfd::State::Up)
    {
      if (m_remoteSessionState == bfd::State::Down)
        setSessionState(bfd::State::Down, bfd::Diag::NeighborSessionDown, SetValueFlags::PreventTxReschedule);
    }
  }

  // TODO if we wanted to go into demand mode, this is where it would happen

  if (isRemoteDemandModeActive())
  {
    // Cease periodic control packets.
    LogVerifyFalse("We do not currently support demand mode");
    m_transmitNextTimer->Stop();
  }
  else if (m_transmitNextTimer->IsStopped())
  {
    // Start the timer.
    scheduleTransmit();
  }

  if (header.GetPoll())
  {
    // If poll was received then send a final response asap, "without respect to
    // the transmission timer" (v10/6.8.7)
    m_pollReceived = true;
    sendControlPacket();
  }

  // If we are active, then we may not yet have a source port
  if (m_remoteSourcePort == 0 && m_isActive)
    m_remoteSourcePort = port;
  else if (m_remoteSourcePort != port)
  {
    m_remoteSourcePort = port;
    gLog.Optional(Log::Session, "Source port has changed for session %u.", m_id);
  }

  // If we were timing out, we no longer are.
  m_timeoutStatus = TimeoutStatus::None;


  // Certain changes my require a packet reschedule.
  if (m_immediateControlPacket
      || olduseDesiredMinTxInterval != getUseDesiredMinTxInterval()
      || oldRemoteMinRxInterval > m_remoteMinRxInterval // v10/6.8.3p6
      || (oldRemoteMinRxInterval == 0 && oldRemoteMinRxInterval != m_remoteMinRxInterval) // basically the same as above.
     )
  {
    scheduleTransmit();
  }

  // Packet received ... update Detection time timer
  scheduleReceiveTimeout();

  return true;
}

/**
 * Gets the time between receiving remote control packets that should be
 * considered a "timeout".
 *
 * Returns 0 if we do not expect any packets. (Timeout disabled.)
 *
 */
uint64_t Session::getDetectionTimeout()
{
  if (getUseRequiredMinRxInterval() == 0)
    return 0;

  return m_remoteDetectMult * uint64_t(max(getUseRequiredMinRxInterval(),  m_remoteDesiredMinTxInterval));
}

/**
 * Schedule the next received timeout based on the current settings.
 *
 */
void  Session::scheduleReceiveTimeout()
{
  // Reset received timer (if any) using UpdateMicroTimer()
  if (!LogVerify(!m_demandMode))
  {
    // We currently never set demand mode
    return;
  }

  uint64_t timeout = getDetectionTimeout();
  if (timeout == 0)
    m_receiveTimeoutTimer->Stop();
  else
    m_receiveTimeoutTimer->SetMicroTimer(timeout);
}

/**
 * Change the received timeout based on the current settings.
 */
void  Session::reScheduleReceiveTimeout()
{
  // Reset received timer (if any) using UpdateMicroTimer()
  if (!LogVerify(!m_demandMode))
  {
    // We currently never set demand mode
    return;
  }

  uint64_t timeout = getDetectionTimeout();
  if (timeout == 0)
    m_receiveTimeoutTimer->Stop();
  else
    m_receiveTimeoutTimer->UpdateMicroTimer(timeout);
}



bool Session::isRemoteDemandModeActive()
{
  return (m_remoteDemandMode && m_sessionState == bfd::State::Up && m_remoteSessionState == bfd::State::Up);
}

/**
 * Use this to change m_sessionState. Handles the timing of control packets.
 *
 * @note This may change the MinTXInterval.
 *
 *
 * Set the SetValueFlags::PreventTxReschedule flag is in flags to prevent this
 * function from calling scheduleTransmit(). Use only if caller will call
 * scheduleTransmit(), or sendControlPacket().
 *
 * Set the SetValueFlags::TryPoll flag is in flags to start a poll sequence if
 * the sate is changed. This is an 'optional' poll sequence, and will be ignored
 * if there is already one underway. The poll sequence may be "ambiguous" (see
 * transitionPollState)
 *
 *
 * @param newState
 * @param diag [in] - The new local diag.
 * @param flags [in] - See description above.
 */
void Session::setSessionState(bfd::State::Value newState, bfd::Diag::Value diag, SetValueFlags::Flag flags /*SetValueFlags::None*/)
{
  if (m_forcedState)
  {
    LogOptional(Log::SessionDetail, "(id=%u) Session held at %s no transition to %s", m_id, bfd::StateName(m_sessionState),  bfd::StateName(newState));
    return;
  }

  m_localDiag = diag;

  if (m_sessionState != newState)
  {
    LogOptional(Log::Session, "(id=%u) Session transition from %s to %s", m_id, bfd::StateName(m_sessionState),  bfd::StateName(newState));

    char const* hook = getenv("OPENBFDD_TRANSITION_HOOK");
    if (hook != NULL && access(hook, X_OK) == 0) {
      string cmd = string(hook) +
        " " + m_localAddr.ToString() + " " + m_remoteAddr.ToString() +
        " " + bfd::StateName(m_sessionState) + " " + bfd::StateName(newState);
      (void)system(cmd.c_str());
    }

    m_sessionState = newState;

    logSessionTransition();

    if (newState == bfd::State::Up)
    {
      // Since we are up, we can change to our real DesiredMinTxInterval
      if (m_desiredMinTxInterval != m_defaultDesiredMinTxInterval)
        setDesiredMinTxInterval(m_defaultDesiredMinTxInterval, (flags & SetValueFlags::PreventTxReschedule));
    }
    else
    {
      // According to v10/6.8.3 when state is not up we must have
      // m_desiredMinTxInterval at least 1000000
      if (m_desiredMinTxInterval < bfd::BaseMinTxInterval)
        setDesiredMinTxInterval(bfd::BaseMinTxInterval, (flags & SetValueFlags::PreventTxReschedule));

      // If we were waiting for a RequiredMinRxInterval change to finish polling, that
      // is now moot.
      if (getUseRequiredMinRxInterval() != m_requiredMinRxInterval)
      {
        // This is Ok here only since we know we are not up.
        gLog.Optional(Log::Session, "(id=%u) RequiredMinRxInterval now using new value %u due to session down.", m_id, m_requiredMinRxInterval);
        setUseRequiredMinRxInterval(m_requiredMinRxInterval);
        reScheduleReceiveTimeout();
      }
    }

    if (SetValueFlags::TryPoll == (flags & SetValueFlags::TryPoll))
      transitionPollState(PollState::Requested, true /*allowAmbiguous*/);

    // Change in control packets should cause an immediate send (per v10/6.8.7)
    m_immediateControlPacket = true;
    if ((flags & SetValueFlags::PreventTxReschedule) != SetValueFlags::PreventTxReschedule)
      scheduleTransmit(); // schedule immediate transmit.
  }
}

/**
 * Logs a transition to the current state.
 * Stores the transition information for stats.
 */
void Session::logSessionTransition()
{
  UptimeInfo *last = NULL;
  TimeSpec now(TimeSpec::MonoNow());

  // We only log state change when we are fully up.
  // The state machine does not allow up->init transition, so we must have been
  // down (and we still count this as down).
  if (m_sessionState == bfd::State::Init)
    return;

  // Check if we even need to log the state transition
  if (!m_uptimeList.empty())
  {
    last = &m_uptimeList.front();

    // only log when state changes.
    if (last->state == m_sessionState)
    {
      if (m_forcedState  && !last->forced)
        last->forced = m_forcedState;
      return;
    }

    // If we go Down->AdminDown, then just call it that.
    if (last->state == bfd::State::Down && m_sessionState == bfd::State::AdminDown)
    {
      last->state = bfd::State::AdminDown;
      last->forced = m_forcedState;
      return;
    }
  }

  GetMonolithicTime(now);

  if (last)
    last->endTime = now;

  // log the new state
  UptimeInfo uptime;

  uptime.state = m_sessionState;
  uptime.startTime = now;
  uptime.forced = false;  // currently not using this.

  try
  {
    m_uptimeList.push_front(uptime);
  }
  catch  (std::exception)
  {
    // TODO - could mark the whole thing as "invalid"?
    m_uptimeList.clear();
  }

  if (m_uptimeList.size() > MaxUptimeCount)
    m_uptimeList.pop_back();
}


/**
 * Called to attempt to transition poll state. Enforces linear transitions.
 *
 * @param nextState
 * @param allowAmbiguous - If true, then we can start a new poll sequence even if
 *                       the previous one just ended. The poll sequence will be
 *                       "ambiguous" as described in v10/6/8/3p9. This should be
 *                       used only if the caller does not need to take any
 *                       action when the poll completes.
 *
 * @return - false if the transition was not valid.
 */
bool Session::transitionPollState(PollState::Value nextState, bool allowAmbiguous /*false*/)
{
  // This is used to track polling. In particular, we can not start two separate
  // polls too close together or they become ambiguous as described at the end of
  // v10/6.8.3. Currently we require a non-F response in between to disambiguate
  // poll. We could also add a timing element as described in that section, if
  // needed.

  if (nextState == PollState::None)
  {
    if (m_pollState == PollState::None)
      return true;

    if (m_pollState == PollState::Completed)
    {
      // Poll sequence is now unambiguously finished. We can now start a new one if we
      // want.
      m_pollState = PollState::None;


      if (m_wantsPollForNewDesiredMinTxInterval
          || m_wantsPollForNewRequiredMinRxInterval
         )
        return transitionPollState(PollState::Requested);

      return true;
    }
    return false;
  }
  else if (nextState == PollState::Requested)
  {
    if (m_pollState == PollState::Requested || m_pollState == PollState::None)
    {
      m_pollState = PollState::Requested;

      // If either of these were true, then we can set them to false now, since they
      // will automatically take effect at the end of this poll sequence.
      m_wantsPollForNewDesiredMinTxInterval = false;
      m_wantsPollForNewRequiredMinRxInterval = false;

      // If we are not transmitting (perhaps m_remoteMinRxInterval == 0) then we need
      // to now to make polling happen. Note that this will still wait until the
      // "next" transmit, even though that might not be required in all cases.
      if (m_transmitNextTimer->IsStopped())
        scheduleTransmit();

      return true;
    }

    if (m_pollState == PollState::Completed && allowAmbiguous)
    {
      m_pollState = PollState::None;
      return transitionPollState(PollState::Requested);
    }

    return false;
  }
  else if (nextState == PollState::Polling)
  {
    if (m_pollState == PollState::Requested || m_pollState == PollState::None)
    {
      m_pollState = PollState::Polling;
      return true;
    }
    return false;
  }
  else if (nextState == PollState::Completed)
  {
    if (m_pollState == PollState::Polling)
    {
      m_pollState = PollState::Completed;

      // Handle any changes that need to be made after polling.

      // we can use the m_desiredMinTxInterval once the poll has completed, unless we
      // want another poll for that purpose.
      if (!m_wantsPollForNewDesiredMinTxInterval)
        setUseDesiredMinTxInterval(m_desiredMinTxInterval);


      // we can use the m_requiredMinRxInterval once the poll has completed, unless we
      // want another poll for that purpose.
      if (!m_wantsPollForNewRequiredMinRxInterval && getUseRequiredMinRxInterval() != m_requiredMinRxInterval)
      {
        setUseRequiredMinRxInterval(m_requiredMinRxInterval);
        reScheduleReceiveTimeout();
      }

      // If we are only sending packets as part of a poll then we can stop now.
      if (!m_transmitNextTimer->IsStopped() && getBaseTransmitTime() == 0)
        scheduleTransmit();

      return true;
    }
    return false;
  }

  LogVerify(false);
  return false;
}


/**
 * Schedules the next packet transmission based on current state.
 * Poll response packets are not scheduled, and so are not handled here.
 */
void Session::scheduleTransmit()
{
  uint64_t transmitInterval = 0;

  // Poll packet responses are handled immediately, so this routine should not be
  // involved.
  LogAssert(!m_pollReceived);

  if (m_immediateControlPacket)
  {
    // On certain changes we need to send an immediate packet
    m_transmitNextTimer->SetMicroTimer(0);
    return;
  }


  if (!m_isActive && m_remoteDiscr == 0)
  {
    m_transmitNextTimer->Stop();
    return; // Passive session.
  }

  // We recalculate the transmit interval every time, which may mean a little
  // extra "churning" on the timer.
  transmitInterval = getBaseTransmitTime();
  if (transmitInterval == 0)
  {
    bool sendPoll = (m_pollState == PollState::Requested || m_pollState == PollState::Polling);
    // If we are not polling, then no packets get sent.
    if (!sendPoll)
    {
      m_transmitNextTimer->Stop();
      return;
    }
    else
    {
      // We are polling, but we have demand mode (not supported anyway) or
      // m_remoteMinRxInterval of 0 (not handled properly by JUNOS 8.5)
      // So we just keep transmitting until we get a poll response. The timing is not
      // spelled out in the spec. TODO: still verifying that this is a reasonable
      // approach.
      transmitInterval = getUseDesiredMinTxInterval();
    }
  }

  // Apply jitter
  transmitInterval = uint64_t(transmitInterval * (0.75 + 0.25 * double(rand()) / RAND_MAX));

  // Limits on jitter
  if (m_detectMult == 1)
  {
    if (transmitInterval > uint64_t(0.90 * transmitInterval))
      transmitInterval = uint64_t(0.90 * transmitInterval);
  }

  m_transmitNextTimer->UpdateMicroTimer(transmitInterval);
}

/**
 *
 * Gets the base time for transmitting periodic control packets.
 *
 * @return uint32_t  - 0 if no packets are being sent.
 */
uint32_t Session::getBaseTransmitTime()
{
  if (!m_isActive && m_remoteDiscr == 0)
    return 0; // Passive session.

  if (m_remoteMinRxInterval == 0)
    return 0; // no periodic

  // Note, we do not handle this anyway.
  if (isRemoteDemandModeActive())
    return 0; // no periodic

  return max(getUseDesiredMinTxInterval(), m_remoteMinRxInterval);
}


/**
 * Sends a control packet. Does not update timers.
 * @note Must be called from main thread.
 *
 */
void Session::sendControlPacket()
{
  bool poll;

  poll = (!m_pollReceived && (m_pollState == PollState::Requested || m_pollState == PollState::Polling));

  BfdPacket packet = BfdPacket();
  BfdPacketHeader &header = packet.header;

  header.SetVersion(bfd::Version);
  header.length = sizeof(header);

  header.SetDiag(m_localDiag);
  header.SetState(m_sessionState);
  header.SetPoll(poll);
  header.SetFinal(m_pollReceived);
  header.SetControlPlaneIndependent(m_controlPlaneIndependent);
  // The next few are always false, so we could skip setting them. Included for
  // completeness.
  header.SetAuth(false);  // never for now.
  header.SetDemand(false);  // never for now.
  header.SetMultipoint(false),  // never
     header.detectMult = m_detectMult;
  header.myDisc = htonl(m_localDiscr);
  header.yourDisc = htonl(m_remoteDiscr);
  header.txDesiredMinInt = htonl(m_desiredMinTxInterval);
  header.rxRequiredMinInt = htonl(m_requiredMinRxInterval);
  header.rxRequiredMinEchoInt = htonl(0);  // no echo allowed for this system.

  // Since we will have tried to send a poll response, we are done unless we get another.
  m_pollReceived = false;

  // Since we are attempting to send the packet, we have fulfilled m_immediateControlPacket
  m_immediateControlPacket = false;

  if (!ensureSendSocket())
    return;

  send(packet);

  if (poll)
    transitionPollState(PollState::Polling);
}

/**
 * Sends the given packet.
 *
 * @note Must be called from main thread.
 *
 * @param packet
 */
void Session::send(const BfdPacket &packet)
{
  if (!ensureSendSocket())
    return;

  if (m_isSuspended)
  {
    gLog.Optional(Log::Packet, "Not sending packet for suspended session %u.", m_id);
    return;
  }

  logPacketContents(packet, true, false, m_remoteAddr, 0, m_localAddr, m_sendPort);

  if (m_sendSocket.SendTo(&packet, packet.header.length,
                          SockAddr(m_remoteAddr, bfd::ListenPort),
                          MSG_NOSIGNAL))
    gLog.Optional(Log::Packet, "Sent control packet for session %u.", m_id);
}

/**
 * Attempts to connect the m_sendSocket send socket, if there is not one
 * already.
 *
 * @return bool - false if the socket could not be opened.
 */
bool Session::ensureSendSocket()
{
  uint16_t startPort;
  Socket sendSocket;
  SockAddr sendAddr;

  if (!m_sendSocket.empty())
    return true;

  m_sendSocket.SetExpectedVerbosity(Log::Warn);

  if (!LogVerify(m_localAddr.IsValid()))
    return false;
  if (!LogVerify(!m_localAddr.IsAny()))
    return false;

  char tmp[255];
  sendSocket.SetLogName(FormatStr(tmp, sizeof(tmp), "Session %d sock", m_id));

  // Note that all sockets will log errors, so we do not have to.
  if (!sendSocket.OpenUDP(m_localAddr.Type()))
    return false;

  if (!sendSocket.SetTTLOrHops(bfd::TTLValue))
    return false;

  /* Find an available port in the proper range */
  if (m_sendPort != 0)
    startPort = m_sendPort;
  else
    startPort = bfd::MinSourcePort + rand() % (bfd::MaxSourcePort - bfd::MinSourcePort);

  sendAddr = SockAddr(m_localAddr, startPort);

  // We need the socket to be quiet so we do not get warnings for each port tried.
  RaiiObjCallVar<bool, Socket, bool, &Socket::SetQuiet> m_socketQuiet(&sendSocket);
  m_socketQuiet = sendSocket.SetQuiet(true);

  while (!sendSocket.Bind(sendAddr))
  {
    switch (sendSocket.GetLastError())
    {
    default:
      gLog.LogError("Unable to open socket for session %" PRIu32 " %s : (%d) %s",
                    m_id, sendAddr.ToString(false),
                    sendSocket.GetLastError(), SystemErrorToString(sendSocket.GetLastError()));
      return false;
      break;
    case EAGAIN:
    case EADDRINUSE:
      // Fall through and keep looking
      break;
    }
    if (sendAddr.Port() == bfd::MaxSourcePort)
      sendAddr.SetPort(bfd::MinSourcePort);
    else
      sendAddr.SetPort(sendAddr.Port() + 1);

    if (sendAddr.Port() == startPort)
    {
      gLog.LogError("Cant find valid send port.");
      return false;
    }
  }
  m_socketQuiet.Dispose(); // Allow log messages again

  if (m_sendPort != 0 && sendAddr.Port() != m_sendPort)
  {
    gLog.Message(Log::Session, "Source port for session %u at address %s changed from %hu to %hu.",  m_id, m_localAddr.ToString(), m_sendPort, (uint16_t)sendAddr.Port());
  }
  else
  {
    gLog.Optional(Log::Session, "Source socket %s for session %u opened.", sendAddr.ToString(), m_id);
  }

  m_sendPort = sendAddr.Port();
  m_sendSocket.Transfer(sendSocket);
  m_sendSocket.SetLogName(sendSocket.LogName());
  return true;
}


/**
 * Called when m_receiveTimeoutTimer expires.
 * This may call "delete this" so the object should not be used again.
 *
 * @param timer
 */
void Session::handleReceiveTimeoutTimer(Timer *ATTR_UNUSED(timer))
{
  // We have timed out.

  // Check that we are still using timeouts.
  if (!LogVerify(getDetectionTimeout() != 0))
    return;

  gLog.Optional(Log::Session, "Session (id=%u) detection timeout.", m_id);

  // Set RemoteMinRxInterval as recommended in v10/6.8.18
  m_remoteMinRxInterval = 1;
  m_remoteDiscr = 0;  // v10/6.8.1 bfd.RemoteDiscr

  if (m_sessionState == bfd::State::Up || m_sessionState == bfd::State::Init)
  {
    setSessionState(bfd::State::Down, bfd::Diag::ControlDetectExpired);

    // If we have timed out after receiving a "F" bit, then we can consider that
    // the polling is disambiguated.
    if (m_pollState == PollState::Completed)
      transitionPollState(PollState::None);
  }

  if (m_timeoutStatus == TimeoutStatus::None)
  {
    // This is the initial timeout. We now wait for a while.
    m_timeoutStatus = TimeoutStatus::TimedOut;

    uint64_t initialTimeout = getDetectionTimeout() * (m_destroyAfterTimeouts - 1);
    gLog.Optional(Log::SessionDetail, "Session (id=%u) setting initial timeout based on local system timeout multiplier.", m_id);
    m_receiveTimeoutTimer->SetMicroTimer(initialTimeout);
  }
  else if (m_timeoutStatus == TimeoutStatus::TimedOut)
  {
    // Active sessions never get suspended and die.
    if (m_isActive)
      return;

    // Although v10/6.8.1p3 only mandates that we keep a session for one Timeout
    // period, this can, in fact, cause problems. Since the remote system is not
    // required to set _its_ RemoteDiscr until one detection timeout has passed
    // (v10/6.8.1). If we destroy the session, but the remote system is still
    // sending packets with the old discriminator. However, according to v10/6.8.6p8
    // we must discard any packet where "Your Discriminator" does not match. What is
    // worse, the JUNOS8.5S4 that we are testing on actually waits 2 detection times
    // before setting its RemoteDiscr to 0 (in violation of the draft spec.)
    m_timeoutStatus = TimeoutStatus::TxSuspeded;

    // Since we do not know which the remote system is actually using
    // getUseDesiredMinTxInterval() or m_desiredMinTxInterval.
    uint64_t remoteDeadlyTimeout = max(max(getUseDesiredMinTxInterval(), m_desiredMinTxInterval), m_remoteMinRxInterval);

    // in theory we could have just changed remoteDeadlyTimeout, and the remote system could
    // not actually be using it .. in which case we would get this wrong. That is
    // very unlikely and we are not worrying about this now. TODO: poll sequence to
    // track receipt of this?
    remoteDeadlyTimeout *= m_detectMult * m_remoteDestroyAfterTimeouts;


    gLog.Optional(Log::SessionDetail, "Session (id=%u) setting deadly timeout based on remote system Detection interval.", m_id);
    m_receiveTimeoutTimer->SetMicroTimer(remoteDeadlyTimeout);
  }
  else if (m_timeoutStatus == TimeoutStatus::TxSuspeded)
  {
    // This is it ... the delay timeout. We have waited long enough ... goodbye
    // cruel world.
    gLog.Optional(Log::SessionDetail, "Killing session (id=%u) after kill period elapsed.", m_id);
    m_beacon->KillSession(this);
    // WE ARE DELETED!
    return;
  }
  else
    LogVerifyFalse("Unexpected value for m_timeoutStatus");
}


/**
 * Called when m_transmitNextTimer expires.
 *
 * @param timer
 */
void Session::handletTransmitNextTimer(Timer *ATTR_UNUSED(timer))
{
  if (m_timeoutStatus != TimeoutStatus::TxSuspeded)
    sendControlPacket();
  else
    gLog.Optional(Log::SessionDetail,  "Not sending packet because we are in TxSuspend from timing out");

  scheduleTransmit();
}

bfd::State::Value Session::GetState()
{
  LogAssert(m_scheduler->IsMainThread());
  return m_sessionState;
}

void Session::GetExtendedState(ExtendedStateInfo &outState)
{
  LogAssert(m_scheduler->IsMainThread());

  outState.localState = m_sessionState;
  outState.localDiag = m_localDiag;
  outState.remoteState = m_remoteSessionState;
  outState.remoteDiag = m_remoteDiag;

  outState.desiredMinTxInterval = m_desiredMinTxInterval;
  outState.useDesiredMinTxInterval = getUseDesiredMinTxInterval();
  outState.defaultDesiredMinTxInterval = m_defaultDesiredMinTxInterval;

  outState.requiredMinRxInterval = m_requiredMinRxInterval;
  outState.useRequiredMinRxInterval = getUseRequiredMinRxInterval();
  outState.detectMult = m_detectMult;

  outState.remoteDetectMult = m_remoteDetectMult;
  outState.remoteDesiredMinTxInterval = m_remoteDesiredMinTxInterval;
  outState.remoteMinRxInterval = m_remoteMinRxInterval;

  outState.transmitInterval = getBaseTransmitTime();  // scheduled transmit interval
  outState.detectionTime = getDetectionTimeout(); // Current detection time for timeouts

  outState.isHoldingState = m_forcedState;
  outState.isSuspended = m_isSuspended;

  outState.uptimeList.assign(m_uptimeList.begin(), m_uptimeList.end());
  if (!outState.uptimeList.empty())
    outState.uptimeList.front().endTime = TimeSpec::MonoNow();
}

uint32_t Session::GetLocalDiscriminator()
{
  LogAssert(m_scheduler->IsMainThread());
  return m_localDiscr;
}

uint32_t Session::GetRemoteDiscriminator()
{
  LogAssert(m_scheduler->IsMainThread());
  return m_remoteDiscr;
}


uint32_t Session::GetId()
{
  LogAssert(m_scheduler->IsMainThread());
  return m_id;
}

void Session::ForceDown(bfd::Diag::Value diag)
{
  forceState(bfd::State::Down,  diag);
}

void Session::ForceAdminDown(bfd::Diag::Value diag)
{
  forceState(bfd::State::AdminDown,  diag);
}


void Session::forceState(bfd::State::Value state, bfd::Diag::Value diag)
{
  LogAssert(m_scheduler->IsMainThread());

  const char *name = bfd::StateName(state);
  LogAssert(state == bfd::State::AdminDown || state == bfd::State::Down);

  if (m_sessionState == state)
  {
    m_localDiag = diag;
    gLog.Optional(Log::Session, "(id=%u) Holding %s session already in %s state.", m_id, name, name);
    m_forcedState = true;
    return;
  }

  // We can change the local state at any time, regardless of other pending
  // changes.
  gLog.Optional(Log::Session, "(id=%u) Holding %s session.", m_id, name);
  m_forcedState = false;  // so that we do not block ourselves.
  setSessionState(state, diag);
  m_forcedState = true;
}

void Session::AllowStateChanges()
{
  if (!m_forcedState)
    return;

  // Let nature take its course.
  m_forcedState = false;

  gLog.Optional(Log::Session, "(id=%u) No longer holding session state.", m_id);

  if (m_sessionState == bfd::State::AdminDown)
  {
    // Transition from Admin to Down to allow us to go back up.
    setSessionState(bfd::State::Down, m_localDiag,
                    m_adminUpPollWorkaround ? SetValueFlags::TryPoll : SetValueFlags::None);
  }
  else if (m_sessionState == bfd::State::Down)
  {
    // If the remote system is in the Down or Init state we could transition
    // directly to Init or Up (respectively.) However, this is not, technically
    // allowed by the spec (though it should be ok.) So for now, we will settle for
    // a poll, which should accelerate the process.
    transitionPollState(PollState::Requested, true /*allowAmbiguous*/);
    m_immediateControlPacket = true;
    scheduleTransmit();
  }
}


void Session::SetSuspend(bool suspend)
{
  bool wasSuspened = m_isSuspended;

  m_isSuspended = suspend;

  gLog.Optional(Log::Session, "(id=%u) set from %s to %s.", m_id, wasSuspened ? "suspended" : "responsive", m_isSuspended ? "suspended" : "responsive");
}

/**
 * Logs packet contents if PacketContents is enabled.
 * This version allows the ports to be specified.
 *
 * @param packet
 * @param outPacket
 * @param inHostOrder
 * @param remoteAddr
 * @param remotePort  - 0 for no port specified.
 * @param localAddr
 * @param localPort - 0 for no port specified.
 */
void Session::logPacketContents(const BfdPacket &packet, bool outPacket, bool inHostOrder, const IpAddr &remoteAddr, in_port_t remotePort, const IpAddr &localAddr, in_port_t localPort)
{
  if (gLog.LogTypeEnabled(Log::PacketContents))
  {
    // Not super efficient ... but we are logging packet contents, so this is a
    // debug situation
    SockAddr useRemoteAddr(remoteAddr);
    SockAddr useLocalAddr(localAddr);
    useRemoteAddr.SetPort(remotePort);
    useLocalAddr.SetPort(localPort);
    doLogPacketContents(packet, outPacket, inHostOrder, useRemoteAddr, useLocalAddr);
  }
}

void Session::LogPacketContents(const BfdPacket &packet, bool outPacket, bool inHostOrder, const SockAddr &remoteAddr, const IpAddr &localAddr)
{
  if (gLog.LogTypeEnabled(Log::PacketContents))
    doLogPacketContents(packet, outPacket, inHostOrder, remoteAddr, SockAddr(localAddr));
}

/**
 * Same as LogPacketContents, but always does the work.
 */
void Session::doLogPacketContents(const BfdPacket &packet, bool outPacket, bool inHostOrder, const SockAddr &remoteAddr, const SockAddr &localAddr)
{
  TimeSpec time;
  const BfdPacketHeader &header = packet.header;

  // Since we use multiple log lines, this is inefficient, and could get
  // "confused" if more than one session was running??
  time  = TimeSpec::MonoNow();

  gLog.Message(Log::PacketContents, "%s [%jd:%09ld] from %s to %s, myDisc=%u yourDisc=%u",
               outPacket ? "Send" : "Receive",
               (intmax_t)time.tv_sec,
               time.tv_nsec,
               localAddr.ToString(),
               remoteAddr.ToString(),
               inHostOrder ? header.myDisc : ntohl(header.myDisc),
               inHostOrder ? header.yourDisc : ntohl(header.yourDisc)
              );

  gLog.Message(Log::PacketContents, "  v=%hhd state=<%s> flags=[%s%s%s%s%s%s] diag=<%s> len=%hhd",
               header.GetVersion(),
               bfd::StateName(header.GetState()),
               header.GetPoll() ? "P" : "",
               header.GetFinal() ? "F" : "",
               header.GetControlPlaneIndependent() ? "C" : "",
               header.GetAuth() ? "A" : "",
               header.GetDemand() ? "D" : "",
               header.GetMultipoint() ? "M" : "",
               bfd::DiagShortString(header.GetDiag()),
               header.length
              );


  gLog.Message(Log::PacketContents, "  Multi=%hhu MinTx=%u MinRx=%u MinEchoRx=%u",
               header.detectMult,
               inHostOrder ? header.txDesiredMinInt :     htonl(header.txDesiredMinInt),
               inHostOrder ? header.rxRequiredMinInt :    htonl(header.rxRequiredMinInt),
               inHostOrder ? header.rxRequiredMinEchoInt : htonl(header.rxRequiredMinEchoInt)
              );
}

void Session::SetMulti(uint8_t val)
{
  LogAssert(m_scheduler->IsMainThread());

  LogAssert(val != 0);

  // Change in control packets should cause an immediate send (per v10/6.8.7)
  if (m_detectMult != val)
  {
    m_detectMult = val;
    m_immediateControlPacket = true;
    scheduleTransmit();
  }
}

void Session::SetMinTxInterval(uint32_t val)
{
  LogAssert(m_scheduler->IsMainThread());

  m_defaultDesiredMinTxInterval = val;

  // Try to change this now .... may cause a packet reschedule.
  setDesiredMinTxInterval(m_defaultDesiredMinTxInterval);
}

void Session::SetMinRxInterval(uint32_t val)
{
  LogAssert(m_scheduler->IsMainThread());
  setRequiredMinRxInterval(val);
}

void Session::SetControlPlaneIndependent(bool cpi)
{
  LogAssert(m_scheduler->IsMainThread());

  // Change in control packets should cause an immediate send (per v10/6.8.7)
  if (m_controlPlaneIndependent != cpi)
  {
    m_controlPlaneIndependent = cpi;
    m_immediateControlPacket = true;
    scheduleTransmit();
  }
}

void Session::SetAdminUpPollWorkaround(bool enable)
{
  LogAssert(m_scheduler->IsMainThread());

  if (m_adminUpPollWorkaround == enable)
    return;

  gLog.Optional(Log::Session, "Session (id=%u) change adminUpPollWorkaround from %s to %s.",
                m_id,
                m_adminUpPollWorkaround ? "enabled" : "disabled",
                enable ? "enabled" : "disabled"
               );

  m_adminUpPollWorkaround = enable;
}

/**
 *
 *
 * @param newValue
 *
 * @param flags [in] - SetValueFlags::PreventTxReschedule to prevent this
 *              function from calling scheduleTransmit(). Use only if caller
 *              will call scheduleTransmit(), or sendControlPacket().
 */
void Session::setDesiredMinTxInterval(uint32_t newValue, SetValueFlags::Flag flags /*SetValueFlags::None*/)
{
  uint32_t oldDesiredMinTxInterval = m_desiredMinTxInterval;
  uint32_t oldUseDesiredMinTxInterval = getUseDesiredMinTxInterval();

  if (m_sessionState != bfd::State::Up && newValue < bfd::BaseMinTxInterval)
  {
    // Per v10/6.8.3p5
    if (newValue < getUseDesiredMinTxInterval() && getUseDesiredMinTxInterval() >  bfd::BaseMinTxInterval)
    {
      // We can still reduce to bfd::BaseMinTxInterval
      newValue = bfd::BaseMinTxInterval;
    }
    else
    {
      gLog.Optional(Log::Session, "(id=%u) DesiredMinTxInterval change to %u ignored since state is not Up.", m_id, newValue);
      return;
    }
  }

  // We can always change this immediately, but it will not effect transmit timing
  // until getUseDesiredMinTxInterval() is changed.
  m_desiredMinTxInterval = newValue;

  if (m_sessionState != bfd::State::Up || newValue <= getUseDesiredMinTxInterval())
  {
    // When not Up, or lowering value, we can change the actual transmission rate.
    setUseDesiredMinTxInterval(newValue);
  }
  else
  {
    // We must wait for poll to complete before changing the transmit rate.
    gLog.Optional(Log::Session, "(id=%u) DesiredMinTxInterval will change from %u to %u after poll sequence.", m_id, oldDesiredMinTxInterval, newValue);
  }

  if (m_pollState != PollState::None && m_pollState != PollState::Requested)
  {
    // If there is already a poll in progress, then we need to finish it before we
    // start a new one for this change.
    m_wantsPollForNewDesiredMinTxInterval = true;

    // Note that this does not prevent us from using the new m_desiredMinTxInterval
    // in control packets immediately (v10/6.8.3)
  }
  else
  {
    // We can never have (m_pollState == PollState:None)  and still have
    // m_wantsPollForNewDesiredMinTxInterval since that would have triggered a new
    // poll sequence. We can not have m_pollState == PollState::Requested and still
    // have m_waitingForPollToChangeState since we are the only ones who would have
    // set it.
    LogVerify(!m_wantsPollForNewDesiredMinTxInterval);
    m_wantsPollForNewDesiredMinTxInterval = false;  // just in case

    // Initiate the poll that is required by 6.8.3
    LogVerify(transitionPollState(PollState::Requested));
  }

  if (oldDesiredMinTxInterval != m_desiredMinTxInterval)
  {
    // Change in control packets should cause an immediate send (per v10/6.8.7)
    m_immediateControlPacket = true;

    if ((flags & SetValueFlags::PreventTxReschedule) != SetValueFlags::PreventTxReschedule)
      scheduleTransmit(); // schedule immediate transmit.
  }
  else if (oldUseDesiredMinTxInterval !=  getUseDesiredMinTxInterval())
  {
    // Change in scheduling (not immediate, just a reschedule)
    if ((flags & SetValueFlags::PreventTxReschedule) != SetValueFlags::PreventTxReschedule)
      scheduleTransmit(); // schedule immediate transmit.
  }
}

/**
 * @param newValue
 *
 * @param flags [in] - SetValueFlags::PreventTxReschedule to prevent this
 *              function from calling scheduleTransmit(). Use only if caller
 *              will call scheduleTransmit(), or sendControlPacket().
 */
void Session::setRequiredMinRxInterval(uint32_t newValue, SetValueFlags::Flag flags /*SetValueFlags::None*/)
{
  uint32_t oldRequiredMinRxInterval = m_requiredMinRxInterval;
  uint32_t oldUseRequiredMinRxInterval = getUseRequiredMinRxInterval();

  // We can always change this immediately, but it will not effect detection
  // timing until getUseRequiredMinRxInterval() is changed.
  m_requiredMinRxInterval = newValue;

  if (m_sessionState != bfd::State::Up || newValue >= getUseRequiredMinRxInterval() || newValue == 0)
  {
    // When not Up, or increasing value (0 is also 'increasing'), we can change the
    // actual detection time.
    setUseRequiredMinRxInterval(newValue);
  }
  else
  {
    // We must wait for poll to complete before changing the detection time.
    gLog.Optional(Log::Session, "(id=%u) RequiredMinRxInterval will change from %u to %u after poll sequence.", m_id, oldRequiredMinRxInterval, newValue);

  }

  if (m_pollState != PollState::None && m_pollState != PollState::Requested)
  {
    // If there is already a poll in progress, then we need to finish it before we
    // start a new one for this change.
    m_wantsPollForNewRequiredMinRxInterval = true;

    // Note that this does not prevent us from using the new m_requiredMinRxInterval
    // in control packets immediately (v10/6.8.3)
  }
  else
  {
    // We can never have (m_pollState == PollState:None)  and still have
    // m_wantsPollForNewRequiredMinRxInterval since that would have triggered a new
    // poll sequence. We can not have m_pollState == PollState::Requested and still
    // have m_waitingForPollToChangeState since we are the only ones who would have
    // set it.
    LogVerify(!m_wantsPollForNewRequiredMinRxInterval);
    m_wantsPollForNewRequiredMinRxInterval = false;  // just in case

    // Initiate the poll that is required by 6.8.3
    LogVerify(transitionPollState(PollState::Requested));
  }

  if (oldRequiredMinRxInterval != m_requiredMinRxInterval)
  {
    // Change in control packets should cause an immediate send (per v10/6.8.7)
    m_immediateControlPacket = true;

    if ((flags & SetValueFlags::PreventTxReschedule) != SetValueFlags::PreventTxReschedule)
      scheduleTransmit(); // schedule immediate transmit.
  }

  // We may also be able to immediately change detection timer.
  if (oldUseRequiredMinRxInterval !=  getUseRequiredMinRxInterval())
  {
    // Change in timeout
    reScheduleReceiveTimeout();
  }
}

void Session::setUseDesiredMinTxInterval(uint32_t val)
{
  if (_useDesiredMinTxInterval != val)
  {
    gLog.Optional(Log::Session, "(id=%u) Active DesiredMinTxInterval change from %u to %u.", m_id, _useDesiredMinTxInterval, val);
    _useDesiredMinTxInterval = val;
  }
}

void Session::setUseRequiredMinRxInterval(uint32_t val)
{
  if (_useRequiredMinRxInterval != val)
  {
    gLog.Optional(Log::Session, "(id=%u) Active RequiredMinRxInterval change from %u to %u.", m_id, _useRequiredMinRxInterval, val);
    _useRequiredMinRxInterval = val;
  }
}
