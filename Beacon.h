/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**

   The main bfdd-beacon application.

 */
#pragma once
#include "Session.h"
#include "threads.h"
#include "hash_map.h"
#include "RecvMsg.h"
#include "SockAddr.h"
#include <deque>
#include <vector>
#include <set>
#include <list>

struct sockaddr_in;

class Socket;
class Scheduler;

class Beacon
{
public:
  Beacon();
  ~Beacon();

  /**
   * Start the beacon.
   *
   * @param controlPorts [in] - A list of address and port combinations on which
   *                     to listen for control commands.
   * @param listenAddrs [in] - A list of address on which to listen for new BDF
   *                    sessions. 'ANY' is allowed.
   *
   *
   * @return - false on failure
   */
  bool Run(const std::list<SockAddr> &controlPorts, const std::list<IpAddr> &listenAddrs);

  typedef void (*OperationCallback)(Beacon *beacon, void *userdata);

  /**
   * Queue up a shutdown.
   *
   * @Note can be called from any thread.
   *
   */
  void RequestShutdown();

  /**
   * Checks a shutdown has been requested and is pending.
   *
   * @Note can be called from any thread.
   *
   * @return bool
   */
  bool IsShutdownRequested();

  /**
   * Queues a callback to occur on the main thread so that the beacon and sessions
   * can be freely accessed. Be aware that the callback code will be blocking all
   * BFD activity, so keep it brief. In addition, Check IsShutdownRequested() in
   * your callback and exit as soon as possible if a shutdown was requested.
   *
   * @Note can be called from any thread.
   *
   * @param callback [in] - The callback.
   * @param userdata [in]- The user data.
   * @param waitForCompletion [in] - Should this thread sleep until the callback
   *                          has been executed?
   *
   * @return bool - false if the operation was not queued because a shutdown had
   *         already been requested or memory failure.
   */
  bool QueueOperation(OperationCallback callback, void *userdata, bool waitForCompletion);


  /**
   * Starts an active session for the given system. If there is already a session
   * for the given system then this currently does nothing.
   *
   * TODO: should change an existing session from passive to active?
   *
   * @Note can only on the main thread.
   *
   * @param remoteAddr [in] - remote address.
   * @param localAddr [in]- address on which to receive and send packets. .
   *
   * @return bool - false on failure. If there is already a session, then this
   *         returns true.
   */
  bool StartActiveSession(const IpAddr &remoteAddr, const IpAddr &localAddr);

  /**
   * Allows us to accept connections from the given ip address.
   *
   * @Note can be called only on the main thread.
   *
   * @throw - yes.
   *
   */
  void AllowPassiveIP(const IpAddr &addr);

  /**
   * Stops us from accepting new connections from the given ip address.
   * If AllowAllPassiveConnections() is enabled, then this is ignored.
   *
   * If there is already an active session for this IP, then it is not effected.
   *
   * @Note can be called only on the main thread.
   *
   */
  void BlockPassiveIP(const IpAddr &addr);

  /**
   * Allow or disallow accepting every invitation.
   *
   * @Note can be called only on the main thread.
   *
   * @param allow
   */
  void AllowAllPassiveConnections(bool allow);

  /**
   * Find Session by discriminator.
   *
   * @Note can be called only on the main thread.
   *
   * @return Session* - NULL on failure
   */
  Session* FindSessionId(uint32_t id);

  /**
   * Find Session by remote and local ip.
   *
   * @Note can be called only on the main thread.
   *
   * @param remoteAddr [in] - Port ignored
   * @param localAddr [in] - Port ignored
   *
   * @return Session* - NULL on failure
   */
  Session* FindSessionIp(const IpAddr &remoteAddr, const IpAddr &localAddr);

  /**
   * Clears and fill the vector with all the sessions.
   *
   * @Note can be called only on the main thread.
   *
   * @throw - yes
   *
   * @param outList
   */
  void GetSessionIdList(std::vector<uint32_t> &outList);

  /**
   * Will delete the session.
   *
   * @Note can be called only on the main thread.
   *
   * Session must be from this beacon.
   */
  void KillSession(Session *session);

  /**
   * Sets the DectectMulti for future sessions.
   *
   * @Note can be called only on the main thread.
   */
  void SetDefMulti(uint8_t val);

  /**
   * Sets the 'default' DesiredMinTXInterval state variable for future sessions.
   *
   * @Note can be called only on the main thread.
   */
  void SetDefMinTxInterval(uint32_t val);

  /**
   * Sets the 'default' RequiredMinTXInterval state variable for future sessions.
   *
   * @Note can be called only on the main thread.
   */
  void SetDefMinRxInterval(uint32_t val);

  /**
   * Changes the default C bit value on for future sessions.
   *
   * @Note can be called only on the main thread.
   */
  void SetDefControlPlaneIndependent(bool cpi);

  /**
   * Enables or disables a workaround that allows fast Up->AdminDown->Up
   * transitions.
   *
   * @Note can be called only on the main thread.
   *
   * @param enable
   */
  void SetDefAdminUpPollWorkaround(bool enable);

private:

  void makeListenSocket(const IpAddr &listenAddr, Socket &outSocket);
  static void handleListenSocketCallback(int socket, void *userdata);
  void handleListenSocket(Socket &socket);

  static void handleSelfMessageCallback(int sigId, void *userdata) { reinterpret_cast<Beacon *>(userdata)->handleSelfMessage(sigId);}
  void handleSelfMessage(int sigId);
  uint32_t makeUniqueDiscriminator();
  bool triggerSelfMessage();

  Session* addSession(const IpAddr &remoteAddr, const IpAddr &localAddr);

  Session* findInSourceMap(const IpAddr &remoteAddr, const IpAddr &localAddr);

private:
  struct  SourceMapKey
  {
    SourceMapKey(IpAddr remoteAddr, IpAddr localAddr) :  remoteAddr(remoteAddr), localAddr(localAddr) { }
    IpAddr remoteAddr;
    IpAddr localAddr;
    bool operator==(const SourceMapKey &other) const { return remoteAddr == other.remoteAddr &&  localAddr == other.localAddr;}
    struct hasher
    {size_t operator ()(const SourceMapKey &me) const { return me.remoteAddr.hash() + me.localAddr.hash();}
    };
  };

  typedef  hash_map<uint32_t, class Session *>::Type DiscMap;
  typedef  DiscMap::iterator DiscMapIt;
  typedef  hash_map<SourceMapKey, class Session *, SourceMapKey::hasher>::Type SourceMap;
  typedef  SourceMap::iterator SourceMapIt;
  typedef  hash_map<uint32_t, class Session *>::Type IdMap;
  typedef  IdMap::iterator IdMapIt;

  // Used to queue up operation
  struct PendingOperation
  {
    inline PendingOperation() : callback(NULL) { }
    OperationCallback callback;
    void *userdata;
    bool completed;
    class WaitCondition *waitCondition; // lock with m_paramsLock
  };

  typedef std::deque<Beacon::PendingOperation *> OperationQueue;


  // All items in this block are used only in the Scheduler thread, so no
  // locking needed
  //
  Scheduler *m_scheduler; // This is only valid after Run() is called.
  RecvMsg m_packet;

  DiscMap m_discMap; // Your Discriminator -> Session
  IdMap m_IdMap; // Human readable session id -> Session
  SourceMap m_sourceMap; // ip/ip -> Session
  std::set<IpAddr, IpAddr::LessClass> m_allowedPassiveIP;
  bool m_allowAnyPassiveIP;
  bool m_strictPorts; // Should incoming ports be limited as described in draft-ietf-bfd-v4v6-1hop-11.txt
  Session::InitialParams m_initialSessionParams;

  // These items are set at startup, so no locking is needed.
  int m_selfSignalId;

  // m_paramsLock locks the parameters that can be adjusted externally. All items
  // in this block are protected by this lock.
  //
  QuickLock m_paramsLock;
  bool m_shutownRequested;
  OperationQueue m_operations;



};
