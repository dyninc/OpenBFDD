/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**
 
   The main bfd-session handler. This guys knows the most about bfd and holds 
   the bfd state. 
    
 */
#pragma once
#include "bfd.h" 
#include "SmartPointer.h" 
#include "TimeSpec.h" 
#include "Socket.h"
#include <list> 

namespace openbfdd
{
  class Beacon;
  class Scheduler;
  class Timer;
  struct BfdPacket;
  class SockAddr;

  /**
   * Session class, handles a single BFD session. 
   * Unless otherwise specified, all calls must be made on the scheduler's main 
   * thread. 
   * 
   */
  class Session
  {
  public:
    struct InitialParams
    {
      InitialParams();
      uint8_t detectMulti;
      uint32_t desiredMinTx;
      uint32_t requiredMinRx;
      bool controlPlaneIndependent;
      bool adminUpPollWorkaround;
    };
       

    /**
     * Creates a new session. 
     * If the maximum number of sessions has been reached (very unlikely) then 
     * GetId() will return 0. Do not use the session is that case.
     *  
     * @throw - std::bad_alloc 
     *  
     * @param scheduler 
     * @param beacon 
     * @param descriminator 
     * @param params 
     */
    Session(Scheduler &scheduler, Beacon *beacon, uint32_t descriminator, const InitialParams &params);
    ~Session();

    /**
     * Starts a passive session receiving packets from the given address and 
     * port. 
     * 
     * @param remoteAddr [in] - remote address. Must include a port, that will be 
     *                   the source port used by the remote machine for sending us
     *                   packets.
     * @param localAddr [in]- address on which to receive and send packets. May not 
     *                  be 'any'
     *  
     * @return - False on failure. 
     */
    bool StartPassiveSession(const SockAddr &remoteAddr, const IpAddr &localAddr);

    /**
     * Starts an active session for the given address. 
     *  
     * This must not already be a working session.
     * 
     * @param remoteAddr [in] - remote address. 
     * @param localAddr [in]- address on which to receive and send packets. May not 
     *                  be 'any'
     *  
     * @return - False on failure. 
     **/ 
    bool StartActiveSession(const IpAddr &remoteAddr, const IpAddr &localAddr);

    /** 
     * Upgrades a passive to active session. 
     *  
     * The session must be a passive session. It is converted to an active one. If
     * the session is passive, and this returns false, then it is still a valid 
     * passive session. 
     * 
     * @return - False on failure. 
     **/ 
    bool UpgradeToActiveSession();


    /**
     * Gets the last remote address set with StartActiveSession() or 
     * StartPassiveSession() 
     * 
     * @return - The address, or an Invalid address if never set. 
     */
    const IpAddr &GetRemoteAddress();

    /**
     * Gets the last local address set with StartActiveSession() or 
     * StartPassiveSession() 
     * 
     * @return - The address, or an Invalid address if never set. 
     */
    const IpAddr &GetLocalAddress();

    /**
     * Checks if the session is active (StartActiveSession)
     * 
     * @return bool 
     */
    bool IsActiveSession();

    /**
     * Takes a wire control packet data and converts it into a BfdPacket. Also does
     * preliminary checks to see if the packet needs to be dropped.
     *  
     * @note Does not need to be called on main thread.  
     *  
     * @param data [in] - Wire format data.
     * @param dataLength [in] - Size of data. 
     * @param outPacket [out] - Filled with the packet info on success. Undefined on 
     *                  failure. May not be NULL.
     * 
     * @return bool - false if the packet should be dropped. 
     */
    static bool InitialProcessControlPacket(const uint8_t* data, size_t dataLength, BfdPacket &outPacket);

    /**
     * Handles the incoming control packet. Assumes that source and destination ip 
     * address have already been matched. 
     *  
     * @note Must be called from main thread.
     * 
     * @param packet 
     * @param port [in] - The source port for the packet. Needed for active role 
     *             only.
     * 
     * @return bool - false if packet was dropped.
     */
    bool ProcessControlPacket(const BfdPacket &packet, in_port_t port);

    /** 
     * Gets the current session state. 
     * 
     * @return bfd::State::Value 
     */
    bfd::State::Value GetState();

    // Tracking uptime
    struct UptimeInfo
    {
      bfd::State::Value state;  // only up, down, or admin down
      TimeSpec startTime; // Start-time
      TimeSpec endTime;   // EndTime-time
      bool forced;  // True if held down (or admin down).
    };


    struct ExtendedStateInfo
    {
      bfd::State::Value localState;
      bfd::Diag::Value localDiag;
      bfd::State::Value remoteState;
      bfd::Diag::Value remoteDiag;

      uint32_t desiredMinTxInterval;
      uint32_t useDesiredMinTxInterval;
      uint32_t defaultDesiredMinTxInterval;
      uint32_t requiredMinRxInterval;
      uint32_t useRequiredMinRxInterval;
      uint8_t detectMult;

      uint8_t remoteDetectMult;
      uint32_t remoteDesiredMinTxInterval; 
      uint32_t remoteMinRxInterval;  

      uint32_t transmitInterval;  // scheduled transmit interval
      uint64_t detectionTime; // Current detection time for timeouts

      bool isHoldingState;
      bool isSuspended;

      std::list<UptimeInfo> uptimeList; // last few transitions. 
    };

    /**
     * Fills outState with extended state information for the session. 
     *  
     * @throw - yes 
     * 
     * @param outState 
     */
    void GetExtendedState(ExtendedStateInfo &outState);

    /** 
     *  
     * Gets the discriminator for this end of the session.
     * 
     * @return uint32_t 
     */        
    uint32_t GetLocalDiscriminator();

    /**
     * Gets the discriminator for the remote end of the session
     * 
     * @return uint32_t 
     */
    uint32_t GetRemoteDiscriminator();

    /**
     * Get the "human readable" id of this session. 
     * The id is never 0 for a valid session. 
     * 
     * @return uint32_t 
     */
    uint32_t GetId();


    /**
     * Forces the session into the "Down" state and holds it there until 
     * AllowStateChanges() is called. 
     */
    void ForceDown(bfd::Diag::Value diag);

    /**
     * Forces the session into the "AdminDown" state and holds it there until 
     * AllowStateChanges() is called. 
     */
    void ForceAdminDown(bfd::Diag::Value diag);

    /**
     * Allows state changes. Call to free session from ForceAdminDown() and 
     * ForceDown(). 
     * 
     */
    void AllowStateChanges();

    /**
     * Suspend or resume session. This is primarily for debugging purposes. 
     * When suspended the session will behave normally, except that it will not 
     * transmit any packets back. 
     * 
     * @param suspend 
     */
    void SetSuspend(bool suspend);

    /**
     * Sets the DectectMulti state variable.
     */
    void SetMulti(uint8_t val);

    /**
     * Sets the 'default' DesiredMinTXInterval state variable for this session. This
     * value will be used when the situation permits, but other factors may 
     * temporarily modify the actual state value. 
     * 
     * @param val 
     */
    void SetMinTxInterval(uint32_t val);

    /**
     * Sets the 'default' RequiredMinTXInterval state variable for this session. It 
     * may take some time for the new timing to take effect.. 
     * 
     * @param val 
     */
    void SetMinRxInterval(uint32_t val);


    /**
     * Changes the C bit on future outgoing packets.
     */
    void SetControlPlaneIndependent(bool cpi);

    /**
     * Enables or disables a workaround that allows fast Up->AdminDown->Up 
     * transitions. 
     *  
     * @param enable 
     */
    void SetAdminUpPollWorkaround(bool enable);

    /**
     * Logs packet contents if PacketContents is enabled.
     * 
     * @param packet 
     * @param outPacket 
     * @param inHostOrder 
     * @param remoteAddr 
     * @param localAddr 
     */
    static void LogPacketContents(const BfdPacket &packet, bool outPacket, bool inHostOrder, const SockAddr &remoteAddr, const IpAddr &localAddr);

    struct SetValueFlags {enum Flag{None=0x0000, PreventTxReschedule = 0x0001, TryPoll = 0x0002};}; 

  private:

    struct PollState
    {
      enum Value
      {
        None, // No polling
        Requested,  // The next control packet should be the first poll sequence.
        Polling,  // A poll has been initiated.
        Completed, // A "final" packet has been received, but no "normal" packet has been received, so it is still not safe to start a new sequence.
      };
    };

    void sendControlPacket();
    void send(const BfdPacket &packet);
    bool isRemoteDemandModeActive();
    void scheduleRecieveTimeout();
    void reScheduleRecieveTimeout();
    uint64_t getDetectionTimeout();
    void scheduleTransmit();
    uint32_t getBaseTransmitTime();


    void setSessionState(bfd::State::Value newState,  bfd::Diag::Value diag = bfd::Diag::None, SetValueFlags::Flag flags = SetValueFlags::None);
    void logSessionTransition();
    bool ensureSendSocket();

    static void handleRecieveTimeoutTimerCallback(Timer *timer, void *userdata) {reinterpret_cast<Session *>(userdata)->handleRecieveTimeoutTimer(timer);}
    void handleRecieveTimeoutTimer(Timer *timer);

    static void handletTransmitNextTimerCallback(Timer *timer, void *userdata) {reinterpret_cast<Session *>(userdata)->handletTransmitNextTimer(timer);}
    void handletTransmitNextTimer(Timer *timer);

    bool transitionPollState(PollState::Value nextState, bool allowAmbiguous = false);
    void forceState(bfd::State::Value state, bfd::Diag::Value diag);
    
    void setDesiredMinTxInterval(uint32_t newValue, SetValueFlags::Flag flags = SetValueFlags::None);  
    void setRequiredMinRxInterval(uint32_t newValue, SetValueFlags::Flag flags = SetValueFlags::None);  

    static void logPacketContents(const BfdPacket &packet, bool outPacket, bool inHostOrder, const IpAddr &remoteAddr, in_port_t remotePort, const IpAddr &localAddr, in_port_t localPort);
    static void doLogPacketContents(const BfdPacket &packet, bool outPacket, bool inHostOrder, const SockAddr &remoteAddr, const SockAddr &localAddr);

  private:


    // Only access m_nextId from main scheduler thread. This is not protected, so if
    // there were more than one scheduler, we would have a problem.
    static uint32_t m_nextId;  // used to generate the human readable id.

    Beacon *m_beacon;      //For lifetime management only. 
    Scheduler *m_scheduler;
    IpAddr m_remoteAddr; 
    in_port_t m_remoteSourcePort;    
    IpAddr m_localAddr; // The ip local address for the session, from which packets are sent. 
    in_port_t m_sendPort; // The port we are using to send to the remote machine for this session.
    bool m_isActive;  // are we taking an active role ... that is, we start sending periodic packets until session comes up. 
    uint32_t m_id;  //Human readable id.


    // For sending data back to the source
    Socket m_sendSocket;

    // State variables from spec
    bfd::State::Value m_sessionState;
    bfd::State::Value m_remoteSessionState;
    uint32_t m_localDiscr;
    uint32_t m_remoteDiscr;
    bfd::Diag::Value m_localDiag;
    uint32_t m_desiredMinTxInterval;  // in microseconds 
    uint32_t m_requiredMinRxInterval; // in microseconds  
    uint32_t m_remoteMinRxInterval;   // in microseconds  
    bool m_demandMode;  // Never set ... we do not currently support this.
    bool m_remoteDemandMode;
    uint8_t m_detectMult;
    bfd::AuthType::Value m_authType;
    uint32_t m_rcvAuthSeq;  // we do not use this since we do not do  MD5 or SHA1 authentication
    uint32_t m_xmitAuthSeq; // we do not use this since we do not do  MD5 or SHA1 authentication 
    bool m_authSeqKnown; // we do not use this since we do not do  MD5 or SHA1 authentication

    // Our state variables
    PollState::Value m_pollState; // Current state of polling.
    bool m_pollRecieved; // Should we set the final bit on the next transmit
    uint8_t m_remoteDetectMult;
    uint32_t m_remoteDesiredMinTxInterval;   // in microseconds  
    bfd::Diag::Value m_remoteDiag;  // last diag from remote system.
    //uint32_t txDesiredMinInt; 
    uint32_t m_destroyAfterTimeouts; // The number of local detection timeouts to wait before destroying the session.
    // The number of _remote_ detection timeouts to wait after we stop sending
    // packets (passive session only)  This could be 1 if the spec was followed, and
    // there was no packet delay. It could be 2 reasonably. But an error in the
    // JUNOS8.5S4 (and possibly others) requires that it be at least 3. (No major harm
    // done though.)
    uint32_t m_remoteDestroyAfterTimeouts;
    struct TimeoutStatus
    {
      enum Value
      {
        None, // not timed out.
        TimedOut,   // Initial timeout period
        TxSuspeded // We are no longer sending packets, and just waiting an appropriate amount of time to die. 
      };
    };

    TimeoutStatus::Value m_timeoutStatus;
    bool m_isSuspended;
    bool m_immediateControlPacket;  // signals that we are looking to send an immediate response due to a state or other change.
    bool m_controlPlaneIndependent;
    bool m_adminUpPollWorkaround;


    // Forced state
    bool m_forcedState; // Are we blocking state transitions?

    bool m_wantsPollForNewDesiredMinTxInterval; // Used only when poll sequence can not be started immediately on  m_desiredMinTxInterval change. 
    uint32_t _useDesiredMinTxInterval; // Used to calculate the transmission interval. 
    uint32_t getUseDesiredMinTxInterval() {return _useDesiredMinTxInterval;}
    void setUseDesiredMinTxInterval(uint32_t val);

    uint32_t m_defaultDesiredMinTxInterval; // Used to store m_desiredMinTxInterval when we are not up.
    bool m_wantsPollForNewRequiredMinRxInterval; // Used only when poll sequence can not be started immediately on _useRequiredMinRxInterval change. 
    uint32_t _useRequiredMinRxInterval; // Used to calculate the timeout interval. 
    uint32_t getUseRequiredMinRxInterval() {return _useRequiredMinRxInterval;}
    void setUseRequiredMinRxInterval(uint32_t val);

    // Keep last few transitions for logging.
    std::list<UptimeInfo> m_uptimeList; 


    // Timers
    void deleteTimer(Timer *timer);
    RiaaClassCall<Timer, Session, &Session::deleteTimer> m_receiveTimeoutTimer; // Timer for the receive packet timeout.
    RiaaClassCall<Timer, Session, &Session::deleteTimer> m_transmitNextTimer;  // Timer for the next control packet.
  };

  inline Session::SetValueFlags::Flag operator|(Session::SetValueFlags::Flag f1, Session::SetValueFlags::Flag f2) {return Session::SetValueFlags::Flag((int)f1 | (int)f2);}
  inline Session::SetValueFlags::Flag operator|=(Session::SetValueFlags::Flag &f1, Session::SetValueFlags::Flag f2) {f1 = f1|f2; return f1;}
  inline Session::SetValueFlags::Flag operator&(Session::SetValueFlags::Flag f1, Session::SetValueFlags::Flag f2) {return Session::SetValueFlags::Flag((int)f1 & (int)f2);}
  inline Session::SetValueFlags::Flag operator&=(Session::SetValueFlags::Flag &f1, Session::SetValueFlags::Flag f2) {f1 = f1&f2; return f1;}
  inline Session::SetValueFlags::Flag operator~(Session::SetValueFlags::Flag f1) {return Session::SetValueFlags::Flag(~(int)f1);}
}

