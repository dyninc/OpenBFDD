/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "Beacon.h"
#include "utils.h"
#include "CommandProcessor.h"
#include "SelectScheduler.h"
#include "KeventScheduler.h"

using namespace std;

struct ListenCallbackData
{
  Beacon *beacon;
  Socket socket;
};
typedef list<ListenCallbackData *> ListenCallbackDataList;
typedef list<CommandProcessor *> CommandProcessorList;

Beacon::Beacon() :
   m_scheduler(NULL),
   m_discMap(32),
   m_IdMap(32),
   m_sourceMap(32),
   m_allowAnyPassiveIP(false),
   m_strictPorts(false),
   m_initialSessionParams(),
   m_selfSignalId(-1),
   m_paramsLock(true),
   m_shutownRequested(false)
{
  // Do as little as possible. Logging not even initialized.
}

Beacon::~Beacon()
{
}

void Beacon::handleListenSocketCallback(int ATTR_UNUSED(socket), void *userdata)
{
  ListenCallbackData *data;
  data = reinterpret_cast<ListenCallbackData *>(userdata);
  data->beacon->handleListenSocket(data->socket);
}

void closeCommandProcessorList(CommandProcessorList *addrList)
{

  if (addrList != NULL)
  {
    for (CommandProcessorList::iterator it = addrList->begin(); it != addrList->end(); ++it)
      delete *it;
  }
  delete addrList;
}


void closeListenCallbackDataList(ListenCallbackDataList *callbackList)
{

  if (callbackList != NULL)
  {
    for (ListenCallbackDataList::iterator it = callbackList->begin(); it != callbackList->end(); ++it)
      delete *it;
  }
  delete callbackList;
}

bool Beacon::Run(const list<SockAddr> &controlPorts, const list<IpAddr> &listenAddrs)
{
  if (m_scheduler != NULL)
  {
    gLog.LogError("Can not call Beacon::Run twice. Aborting.");
    return false;
  }

  if (controlPorts.empty())
  {
    gLog.LogError("At least one control port is required. Aborting.");
    return false;
  }

  RaiiNullBase<CommandProcessorList, closeCommandProcessorList> commandProcessors(new CommandProcessorList);

  for (list<SockAddr>::const_iterator it = controlPorts.begin(); it != controlPorts.end(); ++it)
  {
    CommandProcessor *processor = MakeCommandProcessor(*this);
    commandProcessors->push_back(processor);
    if (!processor->BeginListening(*it))
    {
      gLog.LogError("Failed to start command processing thread on %s.  Aborting.", it->ToString());
      return false;
    }
  }

#ifdef USE_KEVENT_SCHEDULER
  m_scheduler = new KeventScheduler();
#else
  m_scheduler = new SelectScheduler();
#endif

  m_packet.AllocBuffers(bfd::MaxPacketSize,
                        Socket::GetMaxControlSizeReceiveDestinationAddress() +
                        Socket::GetMaxControlSizeReceiveTTLOrHops() +
                        +8 /*just in case*/);

  // We use this "signal channel" to communicate back to ourself in the Scheduler
  // thread.
  if (!m_scheduler->CreateSignalChannel(&m_selfSignalId, handleSelfMessageCallback, this))
  {
    gLog.LogError("Failed to create  self signal handling. Aborting.");
    return false;
  }

  RaiiNullBase<ListenCallbackDataList, closeListenCallbackDataList> callbackData(new ListenCallbackDataList);
  for (list<IpAddr>::const_iterator it = listenAddrs.begin(); it != listenAddrs.end(); ++it)
  {
    ListenCallbackData *data = new ListenCallbackData;
    data->beacon = this;
    callbackData->push_back(data);

    makeListenSocket(*it, data->socket);
    if (data->socket.empty())
    {
      gLog.LogError("Failed to create listen socket for %s on BFD port %hd.", it->ToString(), bfd::ListenPort);
      return false;
    }
    if (!m_scheduler->SetSocketCallback(data->socket, handleListenSocketCallback, data))
    {
      gLog.LogError("Failed to set m_scheduler socket processing for %s. Aborting.", it->ToString());
      return false;
    }
  }

  bool returnVal = false;
  if (!m_scheduler->Run())
    gLog.LogError("Failed to start m_scheduler. Aborting.");
  else
    returnVal = true;

  commandProcessors.Dispose();

  // In theory we should not be using m_scheduler except on the scheduler
  // callbacks, which end when Scheduler::Run() ends
  Scheduler *oldScheduler = m_scheduler;
  m_scheduler = NULL;
  delete oldScheduler;

  return returnVal;
}

bool Beacon::StartActiveSession(const IpAddr &remoteAddr, const IpAddr &localAddr)
{
  Session *session = NULL;

  LogAssert(m_scheduler->IsMainThread());

  session = findInSourceMap(remoteAddr, localAddr);
  if (session)
  {
    if (session->IsActiveSession())
      return true;
    if (!session->UpgradeToActiveSession())
    {
      LogOptional(Log::Session, "Failed to upgrade Session id=%u for %s to %s is to an active session.",
                  session->GetId(),
                  localAddr.ToString(),
                  remoteAddr.ToString()
                 );
      return false;
    }


    LogOptional(Log::Session, "Session id=%u for %s to %s is now an active session.",
                session->GetId(),
                localAddr.ToString(),
                remoteAddr.ToString()
               );
    return true;
  }
  else
  {
    session = addSession(remoteAddr, localAddr);
    if (!session)
      return false;

    LogOptional(Log::Session, "Manually added new session for %s to %s id=%u.",
                localAddr.ToString(),
                remoteAddr.ToString(),
                session->GetId());


    if (!session->StartActiveSession(remoteAddr, localAddr))
    {
      LogOptional(Log::Session, "Failed to start active session id=%u for %s to %s.",
                  session->GetId(),
                  localAddr.ToString(),
                  remoteAddr.ToString()
                 );
      return false;
    }

    LogOptional(Log::Session, "Session id=%u for %s to %s is started as an active session.",
                session->GetId(),
                localAddr.ToString(),
                remoteAddr.ToString()
               );
    return true;
  }
}

void Beacon::AllowPassiveIP(const IpAddr &addr)
{
  LogAssert(m_scheduler->IsMainThread());

  m_allowedPassiveIP.insert(addr);
}

void Beacon::BlockPassiveIP(const IpAddr &addr)
{
  LogAssert(m_scheduler->IsMainThread());

  m_allowedPassiveIP.erase(addr);
}

void Beacon::AllowAllPassiveConnections(bool allow)
{
  LogAssert(m_scheduler->IsMainThread());

  m_allowAnyPassiveIP = allow;
}


void Beacon::RequestShutdown()
{
  gLog.Message(Log::App, "Received shutdown request.");

  AutoQuickLock lock(m_paramsLock);
  m_shutownRequested = true;
  triggerSelfMessage();
}

bool Beacon::IsShutdownRequested()
{
  AutoQuickLock lock(m_paramsLock);
  return m_shutownRequested;
}

Session* Beacon::FindSessionId(uint32_t id)
{
  LogAssert(m_scheduler->IsMainThread());

  IdMapIt found = m_IdMap.find(id);
  if (found == m_IdMap.end())
    return NULL;
  return found->second;
}


Session* Beacon::FindSessionIp(const IpAddr &remoteAddr, const IpAddr &localAddr)
{
  LogAssert(m_scheduler->IsMainThread());
  return findInSourceMap(remoteAddr, localAddr);
}

/**
 *
 * @param remoteAddr [in] - Port ignored
 * @param localAddr [in] - Port ignored
 *
 * @return Session* - NULL on failure
 */
Session* Beacon::findInSourceMap(const IpAddr &remoteAddr, const IpAddr &localAddr)
{
  LogAssert(m_scheduler->IsMainThread());
  SourceMapIt found = m_sourceMap.find(SourceMapKey(remoteAddr, localAddr));
  if (found == m_sourceMap.end())
    return NULL;
  return found->second;
}


void Beacon::GetSessionIdList(std::vector<uint32_t> &outList)
{
  LogAssert(m_scheduler->IsMainThread());

  outList.clear();
  outList.reserve(m_IdMap.size());

  for (IdMapIt found = m_IdMap.begin(); found != m_IdMap.end(); found++)
  {
    outList.push_back(found->first);
  }
}


void Beacon::KillSession(Session *session)
{

  if (!LogVerify(session))
    return;

  LogAssert(m_scheduler->IsMainThread());

  LogVerify(1 == m_discMap.erase(session->GetLocalDiscriminator()));
  LogVerify(1 == m_IdMap.erase(session->GetId()));
  LogVerify(1 == m_sourceMap.erase(SourceMapKey(session->GetRemoteAddress(), session->GetLocalAddress())));

  LogOptional(Log::Session, "Removed session %s to %s id=%d.",
              session->GetLocalAddress().ToString(),
              session->GetRemoteAddress().ToString(),
              session->GetId());

  delete session;
}



bool Beacon::QueueOperation(OperationCallback callback, void *userdata, bool waitForCompletion)
{
  WaitCondition condition(false);
  PendingOperation operation;
  PendingOperation *useOperation;
  Raii<PendingOperation>::Delete allocOperation;


  if (!callback)
  {
    LogAssert(false);
    return false;
  }

  operation.callback = callback;
  operation.userdata = userdata;
  operation.completed = false;
  if (waitForCompletion)
  {
    if (!condition.Init())
      return false;
    operation.waitCondition = &condition;
    useOperation = &operation;
  }
  else
  {
    allocOperation = useOperation = new(std::nothrow) PendingOperation;
    if (!useOperation)
      return false;

    *useOperation = operation;
  }

  {
    AutoQuickLock lock(m_paramsLock);

    if (m_shutownRequested)
      return false;

    try
    {
      m_operations.push_back(useOperation);
    }
    catch (std::exception)
    {
      return false;
    }


    // Once it is in m_operations, then the operation is no longer ours to delete.
    allocOperation.Detach();

    if (waitForCompletion)
    {
      triggerSelfMessage();
      while (!useOperation->completed)
        lock.LockWait(condition);
    }
  }

  if (!waitForCompletion)
    triggerSelfMessage();

  return true;
}

/**
 * Creates the a listen socket on the bfd listen port.
 *
 * @param listenAddr [in] - May be "ANY" (0.0.0.0 or ::)
 *
 * outSocket will be empty on failure.
 *
 *
 */
void Beacon::makeListenSocket(const IpAddr &listenAddr, Socket &outSocket)
{
  Socket listenSocket;

  outSocket.Close();
  // Any socket error will get logged, so we don't need to log them again.
  listenSocket.SetLogName(FormatShortStr("BFD %s listen socket", listenAddr.ToString()));
  listenSocket.OpenUDP(listenAddr.Type());
  if (listenSocket.empty())
    return;

  if (!listenSocket.SetTTLOrHops(bfd::TTLValue))
    return;

  if (!listenSocket.SetReceiveTTLOrHops(true))
    return;

  if (!listenSocket.SetReceiveDestinationAddress(true))
    return;

  if (listenAddr.Type() == Addr::IPv6)
  {
    if (!listenSocket.SetIPv6Only(true))
      return;
  }

  if (!listenSocket.Bind(SockAddr(listenAddr, bfd::ListenPort)))
    return;

  // Take ownership
  outSocket.Transfer(listenSocket);
  outSocket.SetLogName(listenSocket.LogName());

  gLog.Optional(Log::App, "Listening for BFD connections on %s", SockAddr(listenAddr, bfd::ListenPort).ToString());
}

void Beacon::handleListenSocket(Socket &socket)
{
  SockAddr sourceAddr;
  IpAddr destIpAddr, sourceIpAddr;
  uint8_t ttl;
  BfdPacket packet;
  bool found;
  Session *session = NULL;

  if (!m_packet.DoRecvMsg(socket))
  {
    gLog.ErrnoError(m_packet.GetLastError(), "Error receiving on BFD listen socket");
    return;
  }

  sourceAddr = m_packet.GetSrcAddress();
  if (!LogVerify(sourceAddr.IsValid()))
    return;
  sourceIpAddr = IpAddr(sourceAddr);

  destIpAddr = m_packet.GetDestAddress();
  if (!destIpAddr.IsValid())
  {
    gLog.LogError("Could not get destination address for packet from %s.", sourceAddr.ToString());
    return;
  }

  ttl = m_packet.GetTTLorHops(&found);
  if (!found)
  {
    gLog.LogError("Could not get ttl for packet from %s.", sourceAddr.ToString());
    return;
  }

  LogOptional(Log::Packet, "Received bfd packet %zu bytes from %s to %s", m_packet.GetDataSize(), sourceAddr.ToString(), destIpAddr.ToString());

  //
  // Check ip specific stuff. See draft-ietf-bfd-v4v6-1hop-11.txt
  //

  // Port
  if (m_strictPorts)
  {
    if (sourceAddr.Port() < bfd::MinSourcePort) // max port is max value, so no need to check
    {
      LogOptional(Log::Discard, "Discard packet: bad source port %s to %s", sourceAddr.ToString(), destIpAddr.ToString());
      return;
    }
  }

  // TTL assumes that all control packets are from neighbors.
  if (ttl != 255)
  {
    gLog.Optional(Log::Discard, "Discard packet: bad ttl/hops %hhu", ttl);
    return;
  }

  if (!Session::InitialProcessControlPacket(m_packet.GetData(), m_packet.GetDataSize(), packet))
  {
    gLog.Optional(Log::Discard, "Discard packet");
    return;
  }

  // We have a (partially) valid packet ... now find the correct session.
  if (packet.header.yourDisc != 0)
  {
    DiscMapIt found = m_discMap.find(packet.header.yourDisc);
    if (found == m_discMap.end())
    {
      if (gLog.LogTypeEnabled(Log::DiscardDetail))
        Session::LogPacketContents(packet, false, true, sourceAddr, destIpAddr);

      gLog.Optional(Log::Discard, "Discard packet: no session found for yourDisc <%u>.", packet.header.yourDisc);
      return;
    }
    session = found->second;
    if (session->GetRemoteAddress() != sourceIpAddr)
    {
      if (gLog.LogTypeEnabled(Log::DiscardDetail))
        Session::LogPacketContents(packet, false, true, sourceAddr, destIpAddr);

      LogOptional(Log::Discard, "Discard packet: mismatched yourDisc <%u> and ip <from %s to %s>.", packet.header.yourDisc, sourceAddr.ToString(), destIpAddr.ToString());
      return;
    }
  }
  else
  {
    // No discriminator
    session = findInSourceMap(sourceIpAddr, destIpAddr);
    if (NULL == session)
    {
      // No session yet .. create one !?
      if (!m_allowAnyPassiveIP && m_allowedPassiveIP.find(sourceIpAddr) == m_allowedPassiveIP.end())
      {
        if (gLog.LogTypeEnabled(Log::DiscardDetail))
          Session::LogPacketContents(packet, false, true, sourceAddr, destIpAddr);

        LogOptional(Log::Discard, "Ignoring unauthorized bfd packets from %s",  sourceAddr.ToString());
        return;
      }

      session = addSession(sourceIpAddr, destIpAddr);
      if (!session)
        return;
      if (!session->StartPassiveSession(sourceAddr, destIpAddr))
      {
        gLog.LogError("Failed to add new session for local %s to remote  %s id=%d.", destIpAddr.ToString(), sourceAddr.ToString(), session->GetId());
        KillSession(session);
      }
      LogOptional(Log::Session, "Added new session for local %s to remote  %s id=%d.", destIpAddr.ToString(), sourceAddr.ToString(), session->GetId());
    }
  }

  //
  //  We have a session that can handle the rest.
  //
  session->ProcessControlPacket(packet, sourceAddr.Port());
}

/**
 * Adds a session
 *
 * @return Session* - NULL on failure
 */
Session* Beacon::addSession(const IpAddr &remoteAddr, const IpAddr &localAddr)
{
  uint32_t newDisc = makeUniqueDiscriminator();
  Raii<Session>::Delete session;

  try
  {
    session = new Session(*m_scheduler, this, newDisc, m_initialSessionParams);

    if (0 == session->GetId())
      return NULL;

    m_sourceMap[SourceMapKey(remoteAddr, localAddr)] = session;
    m_discMap[newDisc] = session;
    m_IdMap[session->GetId()] = session;
  }
  catch (std::exception &e)
  {
    if (session.IsValid())
    {
      m_sourceMap.erase(SourceMapKey(remoteAddr, localAddr));
      m_IdMap.erase(session->GetId());
    }

    gLog.Message(Log::Error, "Add session failed: %s ", e.what());
    return NULL;
  }

  return session.Detach();
}

/**
 * Call from any thread to trigger handleSelfMessage on main thread.
 *
 *
 * @return bool
 */
bool Beacon::triggerSelfMessage()
{
  if (!LogVerify(m_selfSignalId != -1) || !LogVerify(m_scheduler))
    return false;
  return m_scheduler->Signal(m_selfSignalId);
}


/**
 * Called on the m_scheduler main thread when we have signaled ourselves.
 *
 * @param sigId
 */
void Beacon::handleSelfMessage(int sigId)
{
  (void)sigId;
  AutoQuickLock lock(m_paramsLock);

  while (!m_operations.empty())
  {
    PendingOperation *operation;
    operation = m_operations.front();
    m_operations.pop_front();
    lock.UnLock();

    try
    {
      operation->callback(this,  operation->userdata);
    }
    catch (std::exception &e)
    {
      gLog.Message(Log::Error, "Beacon operation failed: %s ", e.what());
    }

    if (!operation->waitCondition)
      delete operation;
    else
    {
      lock.Lock();
      operation->completed = true;
      lock.SignalAndUnlock(*operation->waitCondition);
    }

    lock.Lock();
  }

  if (m_shutownRequested)
  {
    lock.UnLock();
    m_scheduler->RequestShutdown();
  }
}

/**
 * This creates a "unique" discriminator value. Could be done more methodically to
 * absolutely ensure that we do not re-use a discriminator after a session is
 * removed ... but for now this is probably good enough.
 *
 * @note call only from main thread.
 *
 * @return uint32_t
 */
uint32_t Beacon::makeUniqueDiscriminator()
{
  uint32_t disc = 0;

  // If there were a huge number of sessions this would be inefficient, or even
  // hang. But should be ok.
  while (true)
  {
    disc = rand() % UINT32_MAX;

    if (disc != 0)
    {
      DiscMapIt it = m_discMap.find(disc);
      if (it == m_discMap.end())
        return disc;
    }
  }
}

void Beacon::SetDefMulti(uint8_t val)
{
  LogAssert(m_scheduler->IsMainThread());
  if (!LogVerify(val != 0)) // v10/4.1
    return;

  m_initialSessionParams.detectMulti = val;
}

void Beacon::SetDefMinTxInterval(uint32_t val)
{
  LogAssert(m_scheduler->IsMainThread());
  if (!LogVerify(val != 0)) // v10/4.1
    return;

  m_initialSessionParams.desiredMinTx = val;
}

void Beacon::SetDefMinRxInterval(uint32_t val)
{
  LogAssert(m_scheduler->IsMainThread());
  m_initialSessionParams.requiredMinRx = val;
}

void Beacon::SetDefControlPlaneIndependent(bool cpi)
{
  LogAssert(m_scheduler->IsMainThread());
  m_initialSessionParams.controlPlaneIndependent = cpi;
}

void Beacon::SetDefAdminUpPollWorkaround(bool enable)
{
  LogAssert(m_scheduler->IsMainThread());
  m_initialSessionParams.adminUpPollWorkaround = enable;
}
