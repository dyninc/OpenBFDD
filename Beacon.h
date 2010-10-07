/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**
    
   The main bfdd-beacon application.
    
 */
#pragma once
#include "bfd.h"
#include "Session.h"
#include "threads.h"
#include <deque>
#include <set>
#include <ext/hash_map>

struct sockaddr_in;

namespace openbfdd
{
  class Session;
  class Scheduler;

  class Beacon
  {
  public:
    Beacon();
    ~Beacon();
    int Run();

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
     * @param localAddr [in]- adress on which to recieve and send packets.
     * 
     * @return bool - false on failure. If there is already a session, then this 
     *         returns true.
     */
    bool StartActiveSession(in_addr_t remoteAddr, in_addr_t localAddr);


    /** 
     * Allows us to accept connections from the given ip address. 
     * 
     * @Note can be called only on the main thread. 
     *  
     * @throw - yes. 
     * 
     * @param addr 
     */
    void AllowPassiveIP(in_addr_t addr);

    /** 
     * Stops us from accepting new connections from the given ip address. 
     * If AllowAllPassiveConnections() is enabled, then this is ignored. 
     *  
     * If there is already an active session for this IP, then it is not effected. 
     * 
     * @Note can be called only on the main thread.
     * 
     * @param addr 
     */
    void BlockPassiveIP(in_addr_t addr);


    /** 
     * Allow or disallow accepting every invitation. 
     * 
     * @Note can be called only on the main thread.
     * 
     * @param allow 
     */
    void AllowAllPassiveConnections(bool allow);

    /** 
     * Find Session by discriminator or remote and local ip. 
     * 
     * @Note can be called only on the main thread. 
     *  
     * @return Session* - NULL if none found
     */
    Session *FindSessionId(uint32_t id);
    Session *FindSessionIp(in_addr_t remoteAddr, in_addr_t localAddr);

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

    int makeListenSocket();
    static void handleListenSocketCallback(int socket, void *userdata) {reinterpret_cast<Beacon *>(userdata)->handleListenSocket(socket);}
    void handleListenSocket(int socket);
    size_t readSocketPacket(int listenSocket, struct sockaddr_in *outSourceAddress, in_addr_t *outDstAddress, uint8_t *outTTL);

    static void handleSelfMessageCallback(int sigId, void *userdata) {reinterpret_cast<Beacon *>(userdata)->handleSelfMessage(sigId);}
    void handleSelfMessage(int sigId);
    uint32_t makeUniqueDiscriminator();

    Session *addSession(in_addr_t remoteAddr, in_addr_t localAddr); 

    Session *findInSourceMap(in_addr_t remoteAddr, in_addr_t localAddr);

  private:
    typedef  __gnu_cxx::hash_map<uint32_t, class Session *> DiscMap; 
    typedef  DiscMap::iterator DiscMapIt; 
    typedef  __gnu_cxx::hash_map<uint64_t, class Session *> SourceMap; 
    typedef  SourceMap::iterator SourceMapIt; 
    typedef  __gnu_cxx::hash_map<uint32_t, class Session *> IdMap; 
    typedef  IdMap::iterator IdMapIt; 

    // Used to queue up operation
    struct PendingOperation
    {
      inline PendingOperation() : callback(NULL) {}
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
    std::vector<uint8_t> m_packetBuffer; // main message buffer.
    std::vector<uint8_t> m_messageBuffer;  // for iovec buffer.
    uint8_t m_msgbuf[bfd::MaxPacketSize];
    DiscMap m_discMap; // Your Discriminator -> Session
    IdMap m_IdMap; // Human readable session id -> Session
    SourceMap m_sourceMap; // ip/ip -> Session
    std::set<in_addr_t> m_allowedPassiveIP;
    bool m_allowAnyPassiveIP;
    bool m_strictPorts; // Should incoming ports be limited as described in draft-ietf-bfd-v4v6-1hop-11.txt
    Session::InitialParams m_initialSessionParams;
    
    // m_paramsLock locks the parameters that can be adjusted externally. All items
    // in this block are protected by this lock.
    // 
    QuickLock m_paramsLock;
    bool m_shutownRequested;
    OperationQueue m_operations;


  };
}
