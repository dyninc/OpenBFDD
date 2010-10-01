/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jacob Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "Beacon.h"
#include "utils.h"
#include "CommandProcessor.h"
#include "Scheduler.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>

using namespace std;

namespace openbfdd
{
  Beacon::Beacon() :
  m_scheduler(NULL),
  m_packetBuffer(bfd::MaxPacketSize),
  m_discMap(32),
  m_IdMap(32),
  m_sourceMap(32),
  m_allowAnyPassiveIP(false),
  m_strictPorts(false),
  m_initialSessionParams(),
  m_paramsLock(true),
  m_shutownRequested (false)
  {
    // Do as little as possible. Logging not even initialized.   
    
    // This must be big enough to include ttl and dest address 
    m_messageBuffer.resize(CMSG_SPACE(sizeof(uint32_t))
                           + CMSG_SPACE(sizeof(struct in_addr))
                           + 8 /*just in case*/ 
                             );
  }

  Beacon::~Beacon()
  {
  }

  int Beacon::Run()
  {
    RiaaClass<CommandProcessor> commandProcessor(MakeCommandProcessor(*this));
    RiaaClass<CommandProcessor> altCommandProcessor(MakeCommandProcessor(*this));
    int returnVal = 1;

    if (m_scheduler != NULL)
    {
      gLog.LogError("Can not call Beacon::Run twice. Aborting.");
      return 1;
    }

    if (!commandProcessor->BeginListening(PORTNUM))
    {
      gLog.LogError("Failed to start command processing thread. Aborting.");
      return 1;
    }

    if (!altCommandProcessor->BeginListening(ALT_PORTNUM))
    {
      gLog.LogError("Failed to start alternate command processing thread. Aborting.");
      return 1;
    }

    m_scheduler = Scheduler::MakeScheduler();
    int socket = makeListenSocket();
    if (socket < 0)
    {
      gLog.LogError("Failed to start create listen socket on BFD port %hd.", bfd::ListenPort);
      return 1;

    }

    if (!m_scheduler->SetSocketCallback(socket, handleListenSocketCallback, this))
    {
      gLog.LogError("Failed to set m_scheduler socket processing thread. Aborting.");
      return 1;
    }

    // We use SIGUSR1 to communicate back to ourself in the Scheduler thread. 
    if (!m_scheduler->SetSignalCallback(SIGUSR1, handleSelfMessageCallback, this, true /*disable normal signal handling*/))
    {
      gLog.LogError("Failed to set SIGUSER1 signal handling. Aborting.");
      return 1;
    }

    if (!m_scheduler->Run())
      gLog.LogError("Failed to start m_scheduler. Aborting.");
    else
      returnVal = 0;

    commandProcessor->StopListening();
    commandProcessor.Dispose();

    altCommandProcessor->StopListening();
    altCommandProcessor.Dispose();

    // In theory we should not be using m_scheduler except on the scheduler
    // callbacks, which end when Scheduler::Run() ends
    Scheduler *oldScheduler = m_scheduler;
    m_scheduler = NULL;
    Scheduler::FreeScheduler(oldScheduler);

    return returnVal;
  }

  bool Beacon::StartActiveSession(in_addr_t remoteAddr, in_addr_t localAddr)
  {
    Session *session = NULL;

    LogAssert(m_scheduler->IsMainThread());

    session = findInSourceMap(remoteAddr, localAddr);
    if (NULL == session)
    {
      session = addSession(remoteAddr, localAddr); 
      if (!session)
        return false;

      LogOptional(Log::Session, "Manually added new session for %s to %s id=%u.", 
                  Ip4ToString(localAddr),  
                  Ip4ToString(remoteAddr),  
                  session->GetId());
    }

    if(session->IsActiveSession())
      return true;

    session->StartActiveSession(remoteAddr, localAddr);

    LogOptional(Log::Session, "Session id=%u for %s to %s is now an active session.",
                session->GetId(), 
                Ip4ToString(localAddr),
                Ip4ToString(remoteAddr)  
                );
    return true;
  }

  void Beacon::AllowPassiveIP(in_addr_t addr)
  {
    LogAssert(m_scheduler->IsMainThread());

    m_allowedPassiveIP.insert(addr);
  }

  void Beacon::BlockPassiveIP(in_addr_t addr)
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
    raise(SIGUSR1);
  }

  bool Beacon::IsShutdownRequested()
  {
    AutoQuickLock lock(m_paramsLock);
    return m_shutownRequested;
  }

  Session *Beacon::FindSessionId(uint32_t id)
  {
    LogAssert(m_scheduler->IsMainThread());

    IdMapIt found = m_IdMap.find(id);
    if (found == m_IdMap.end())
      return NULL;
    return found->second;
  }


  Session *Beacon::FindSessionIp(in_addr_t remoteAddr, in_addr_t localAddr)
  {
    LogAssert(m_scheduler->IsMainThread());
    return findInSourceMap(remoteAddr, localAddr);
  }

  static inline uint64_t makeSourceMapKey(in_addr_t remoteAddr, in_addr_t localAddr)
  {
    return (static_cast<uint64_t>(remoteAddr) << 32) + localAddr;
  }

  /**
   * 
   * 
   * @param remoteAddr 
   * @param localAddr 
   * 
   * @return Session* - NULL on failure 
   */
  Session *Beacon::findInSourceMap(in_addr_t remoteAddr, in_addr_t localAddr)
  {
    LogAssert(m_scheduler->IsMainThread());
    SourceMapIt found = m_sourceMap.find(makeSourceMapKey(remoteAddr, localAddr));
    if (found == m_sourceMap.end())
      return NULL;
    return found->second;
  }


  void Beacon::GetSessionIdList(std::vector<uint32_t> &outList)
  {
    LogAssert(m_scheduler->IsMainThread());

    outList.clear();
    outList.reserve(m_IdMap.size());

    for (IdMapIt found=m_IdMap.begin(); found != m_IdMap.end(); found++)
    {
      outList.push_back(found->first);
    }
  }


  void Beacon::KillSession(Session *session)
  {

    if (!LogVerify(session))
      return;

    LogAssert(m_scheduler->IsMainThread());

    LogVerify(1==m_discMap.erase(session->GetLocalDiscriminator()));
    LogVerify(1==m_IdMap.erase(session->GetId()));
    LogVerify(1==m_sourceMap.erase(makeSourceMapKey(session->GetRemoteAddress(), session->GetLocalAddress())));

    LogOptional(Log::Session, "Removed session %s to %s id=%d.", 
                Ip4ToString(session->GetLocalAddress()),  
                Ip4ToString(session->GetRemoteAddress()),  
                session->GetId());

    delete session;
  }



  bool Beacon::QueueOperation(OperationCallback callback, void *userdata, bool waitForCompletion)
  {
    WaitCondition condition(false);
    PendingOperation operation;
    PendingOperation *useOperation;

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
      useOperation = new PendingOperation;
      *useOperation = operation;
    }


    {
      AutoQuickLock lock(m_paramsLock);

      if (m_shutownRequested)
      {
        if (!waitForCompletion)
          delete useOperation;
        return false;
      }

      m_operations.push_back(useOperation);

      if (waitForCompletion)
      {
        raise(SIGUSR1);
        while (!useOperation->completed)
          lock.LockWait(condition);
      }
    }

    if (!waitForCompletion)
      raise(SIGUSR1);

    return true;
  }

  /**
   * Creates the one and only listen socket on the bfd listen port.
   * 
   * 
   * @return int 
   */
  int Beacon::makeListenSocket ()
  {
    int val;
    FileDescriptor listenSocket;
    struct ::sockaddr_in sin;

    /* Make UDP listenSocket to receive control packets */
    listenSocket = ::socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!listenSocket.IsValid())
    {
      gLog.ErrnoError(errno, "Failed to create BFD listen  listenSocket");
      return -1;
    }

    val = bfd::TTLValue;
    if (::setsockopt(listenSocket, IPPROTO_IP, IP_TTL, &val, sizeof(val)) < 0)
    {
      gLog.ErrnoError(errno, "Can't set TTL for outgoing packets");
      return -1;
    }
    val = 1;
    if (::setsockopt(listenSocket, IPPROTO_IP, IP_RECVTTL, &val, sizeof(val)) < 0)
    {
      gLog.ErrnoError(errno, "Can't set receive TTL for incoming packets");
      return -1;
    }

    val = 1;
    if (::setsockopt(listenSocket, IPPROTO_IP, IP_RECVDSTADDR, &val, sizeof(val)) < 0)
    {
      gLog.ErrnoError(errno, "Can't set option to get destination address for incoming packets");
      return -1;
    }
    
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(bfd::ListenPort);
    if (::bind(listenSocket, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
      gLog.ErrnoError(errno, "Can't bind listenSocket to default port for bfd listen listenSocket.");
      return -1;
    }

    return  listenSocket.Detach();
  }

  /**
   *  Gets the tll.
   * 
   * 
   * @param msg 
   * @param outTTL [out] - Set to the ttl on success. May not be null.
   * 
   * @return bool - false on failure.
   */         
  static bool getTTL(struct msghdr *msg, uint8_t *outTTL)
  {
    struct cmsghdr* cmsg;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg))
    {
      // It appears that  some systems use IP_TTL and some use IP_RECVTTL to
      // return the ttl. Specifically, FreeBSD uses IP_RECVTTL and Debian uses
      // IP_TTL. We work around this by checking both (until that breaks some system.)
      if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TTL)
      {  
        if(LogVerify(cmsg->cmsg_len >= CMSG_LEN(sizeof(uint32_t))))
        {  
          *outTTL = (uint8_t)*(uint32_t *)CMSG_DATA(cmsg);
          return true;
        }
      }
      else if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_RECVTTL)
      {  
        *outTTL = *(uint8_t *)CMSG_DATA(cmsg);
        return true;
      }
    }

    return false;
  }

  static bool getDstAddr(struct msghdr *msg, in_addr_t *outDstAddress)
  {
    struct cmsghdr* cmsg;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg))
    {
      if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_RECVDSTADDR)
      {  
        if(cmsg->cmsg_len < CMSG_LEN(sizeof(struct in_addr)))
          return false;

        *outDstAddress = reinterpret_cast<struct in_addr *>(CMSG_DATA(cmsg))->s_addr;
        return true;
      }
    }

    return false;
  }


  /**
   * @param listenSocket 
   * @param outSourceAddress 
   * @param outDstAddress 
   * @param outTTL 
   * 
   * @return size_t - 0 on failure. Otherwize the number of bytes read.
   */
  size_t Beacon::readSocketPacket(int listenSocket, struct sockaddr_in *outSourceAddress, in_addr_t *outDstAddress, uint8_t *outTTL)
  {
    int msgLength;
    struct sockaddr_in *sin;
    struct iovec msgiov;
    msgiov.iov_base = &m_packetBuffer.front();
    msgiov.iov_len =  m_packetBuffer.size();

    struct sockaddr_in msgaddr;
    struct msghdr message; 
    message.msg_name =&msgaddr;
    message.msg_namelen = sizeof(msgaddr);
    message.msg_iov = &msgiov;
    message.msg_iovlen = 1;  // ??
    message.msg_control = &m_messageBuffer.front();
    message.msg_controllen = m_messageBuffer.size();
    message.msg_flags = 0;

    // Get packet
    msgLength = recvmsg(listenSocket, &message, 0);
    if (msgLength < 0)
    {
      gLog.ErrnoError(errno, "Error receiving on BFD listen listenSocket");
      return 0;
    }

    // Get source address
    if (message.msg_namelen < sizeof(struct sockaddr_in))
    {
      gLog.ErrnoError(errno, "Malformed source address on BFD packet.");
      return 0;
    }

    sin = reinterpret_cast<struct sockaddr_in *>(message.msg_name);;
    memcpy(outSourceAddress, sin, sizeof(struct sockaddr_in));
    outSourceAddress->sin_port = ntohs(outSourceAddress->sin_port );

    if (!getTTL(&message, outTTL))
    {
      gLog.LogError("Could not get ttl for packet from %s:%hu.", Ip4ToString(sin->sin_addr), outSourceAddress->sin_port);
      return 0;
    }
       
    if (!getDstAddr(&message, outDstAddress))
    {
      gLog.LogError("Could not get destination address for packet from %s:%hu.", Ip4ToString(sin->sin_addr), outSourceAddress->sin_port);
      return 0;
    }
       
    return msgLength;
  }


  void Beacon::handleListenSocket(int listenSocket)
  {
    struct sockaddr_in sourceAddr;
    in_addr_t destAddr;
    size_t messageLength;
    uint8_t ttl; 
    BfdPacket packet;
    Session *session = NULL;


    messageLength = readSocketPacket(listenSocket, &sourceAddr, &destAddr, &ttl);
    if (messageLength == 0)
      return;

    LogOptional(Log::Packet, "Received bfd packet %zu bytes from %s:%hu to %s", messageLength, Ip4ToString(sourceAddr.sin_addr), sourceAddr.sin_port, Ip4ToString(destAddr));

    // 
    // Check ip specific stuff. See draft-ietf-bfd-v4v6-1hop-11.txt
    //

    // Port
    if(m_strictPorts)
    {
      if (sourceAddr.sin_port < bfd::MinSourcePort) // max port is max value, so no need to check
      {
        LogOptional(Log::Discard, "Discard packet: bad source port %s:%hu to %s", Ip4ToString(sourceAddr.sin_addr), sourceAddr.sin_port, Ip4ToString(destAddr));
        return;
      }
    }

    // TTL assumes that all control packets are from neighbors.
    if (ttl != 255)
    {
      gLog.Optional(Log::Discard, "Discard packet: bad ttl %hhu", ttl);
      return;
    }

    if (!Session::InitialProcessControlPacket(&m_packetBuffer.front(), messageLength, packet))
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
            Session::LogPacketContents(packet, false, true, sourceAddr.sin_addr.s_addr, sourceAddr.sin_port, destAddr, 0);
            
        gLog.Optional(Log::Discard, "Discard packet: no session found for yourDisc <%u>.", packet.header.yourDisc);
        return;
      }
      session = found->second;
      if (session->GetRemoteAddress() != sourceAddr.sin_addr.s_addr)
      {
        if (gLog.LogTypeEnabled(Log::DiscardDetail))
            Session::LogPacketContents(packet, false, true, sourceAddr.sin_addr.s_addr, sourceAddr.sin_port, destAddr, 0);

        LogOptional(Log::Discard, "Discard packet: mismatched yourDisc <%u> and ip <from %s to %s>.", packet.header.yourDisc, Ip4ToString(sourceAddr.sin_addr), Ip4ToString(destAddr));
        return;
      }
    }
    else
    {
      // No discriminator
      session = findInSourceMap(sourceAddr.sin_addr.s_addr, destAddr);
      if (NULL == session)
      {
        // No session yet .. create one !?
        if (!m_allowAnyPassiveIP && m_allowedPassiveIP.find(sourceAddr.sin_addr.s_addr) == m_allowedPassiveIP.end())
        {
          if (gLog.LogTypeEnabled(Log::DiscardDetail))
              Session::LogPacketContents(packet, false, true, sourceAddr.sin_addr.s_addr, sourceAddr.sin_port, destAddr, 0);

          LogOptional(Log::Discard, "Ignoring unauthorized bfd packets from %s",  Ip4ToString(sourceAddr.sin_addr));
          return;
        }

        session = addSession(sourceAddr.sin_addr.s_addr,destAddr); 
        if (!session)
          return;
        session->StartPassiveSession(sourceAddr.sin_addr.s_addr, sourceAddr.sin_port, destAddr);
        LogOptional(Log::Session, "Added new session for local %s to remote  %s:%hu id=%d.", Ip4ToString(destAddr), Ip4ToString(sourceAddr.sin_addr), sourceAddr.sin_port, session->GetId());
      }
    }

    // 
    //  We have a session that can handle the rest. 
    // 
    session->ProcessControlPacket(packet, sourceAddr.sin_port);
  }

  /**
   * Adds a session
   * 
   * @param remoteAddr 
   * @param localAddr 
   * 
   * @return Session* - NULL on failure
   */
  Session *Beacon::addSession(in_addr_t remoteAddr, in_addr_t localAddr)
  {
    uint32_t newDisc = makeUniqueDiscriminator();
    Session *session = new Session(*m_scheduler, this, newDisc, m_initialSessionParams);
    if (0 == session->GetId())
    {
      delete session;
      return NULL;
    }
    m_sourceMap[makeSourceMapKey(remoteAddr, localAddr)] = session;
    m_discMap[newDisc] = session;
    m_IdMap[session->GetId()] = session;
    return session;
  }

  /**
   * Called on the m_scheduler main thread when we have signaled ourselves.
   * 
   * @param sigId 
   */
  void Beacon::handleSelfMessage(int sigId)
  {
    (void)sigId;

    LogAssert(sigId == SIGUSR1);
    {
      AutoQuickLock lock(m_paramsLock);
      while (!m_operations.empty())
      {
        PendingOperation *operation;
        operation = m_operations.front();
        m_operations.pop_front();
        lock.UnLock();

        operation->callback(this,  operation->userdata);
        if (!operation->waitCondition)
          delete operation;
        else
        {
          lock.Lock();
          operation->completed=true;
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
    if(!LogVerify(val != 0)) // v10/4.1
      return;

    m_initialSessionParams.detectMulti = val;
  }

  void Beacon::SetDefMinTxInterval(uint32_t val)
  {
    LogAssert(m_scheduler->IsMainThread());
    if(!LogVerify(val != 0)) // v10/4.1
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




}


