/************************************************************** 
* Copyright (c) 2011, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once

#include "SockAddr.h"
#include "SmartPointer.h"

namespace openbfdd
{

  /**
   * Class for socket. It can perform some basic socket tasks as well. All 
   * functions will log to Log::Error on failure. 
   * It has optional ::close() semantics. 
   * Call UtilsInit() before using. 
   */
  class Socket
  {
  public:

    Socket();

    /** 
     * If family is AF_UNSPEC is used, then some operations may fail. 
     *  
     * @param sock 
     * @param family [in] - The address family that the socket uses 
     * @param owned [in] - Should the socket be closed when this is destroyed. 
     * 
     */
    Socket(int sock, Addr::Type family, bool owned = false);

    /** 
     * Copies socket. The LogName and Quiet values are not copied.
     * 
     * @note The new socket is NOT owned.
     */
    Socket(const Socket &src);


    ~Socket();

    /**
     * Attempts to create a socket. 
     * Will be owned by default. 
     *  
     * @note Use GetLastError() for error code on failure. 
     * 
     * @param family [in] - The address family that the socket uses
     * @param type 
     * @param protocol 
     * 
     * @return bool - false on failure.
     */
    bool Open(Addr::Type family, int type, int protocol);

    /**
     * Same as open, with (family, SOCK_DGRAM, IPPROTO_UDP)
     *  
     * @note Use GetLastError() for error code on failure. 
     *  
     * @param family [in] - The address family that the socket uses
     *  
     * @return bool - false on failure.
     */
    bool OpenUDP(Addr::Type family);

    /**
     * Same as open, with (family, SOCK_STREAM, IPPROTO_TCP)
     *  
     * @note Use GetLastError() for error code on failure. 
     *  
     * @param family [in] - The address family that the socket uses
     *  
     * @return bool - false on failure.
     */
    bool OpenTCP(Addr::Type family);

    /**
     * Attach an existing socket. 
     * There will be no associated address. 
     * If family is AF_UNSPEC is used, then some operations may fail. 
     *  
     * 
     * @param sock 
     * @param family [in] - The address family that the socket uses
     * @param owned [in] - Should the socket be closed when this is destroyed. 
     */
    void Attach(int sock, Addr::Type family, bool owned = false);

    /**
     * Attach an existing socket. 
     * Associate the given address and address family.
     *  
     * @param owned [in] - Should the socket be closed when this is destroyed. 
     *  
     * @param sock 
     * @param addr 
     */
    void Attach(int sock, const sockaddr *addr, socklen_t addrlen, bool owned = false);


    /** 
     * Detach and return the socket. 
     *  
     * This will no longer be responsible for closing the socket.
     * 
     * @return int 
     */
    Socket &Detach() {m_owned = false; return *this;}


    /**
     * If this is a valid socket, then it will be owned. That is it will be closed
     * when the destructor is called, or Close() is called
     */
    void TakeOwnership() {m_owned = true;}


    /** 
     * Makes this a copy of src. This will own the socket if src did, and src will
     * loose ownership (if it had it) 
     * The LogName and Quiet values are not copied.
     */
    void Transfer(Socket &src);

    /**
     * Sets the name used for logging of errors. 
     * 
     * @param str [in] - NULL or empty for no name.
     */
    void SetLogName(const char *str);

    /**
     * Gets the log name.
     * 
     * @return const char* - Do not store. Never NULL. 
     */
    const char *LogName() {return m_logName.c_str();}


    /**
     * Disables logging of errors.
     *  
     * @param quiet 
     * 
     * @return bool - The old quiet value.
     */
    bool SetQuiet(bool quiet);

    /**
     * Returns the socket.
     * 
     * @return int 
     */
    int GetSocket() {return m_socket;}

    /**
     * @return - The error from the last call. 0 if it succeeded. 
     * @note only calls that specificaly state that they set the error are 
     *       guaranteed to do so. Others may, or may not.
     *  
     */
    int GetLastError() {return m_error;}

    /** 
     *  
     * Close the socket only if owned. 
     * Otherwise, it simply clears the socket.
     */
    void Close();

    /** 
     * Closes the socket. 
     * This will close the socket even if it is not owned. 
     * 
     */
    void AlwaysClose();


    /**
     * Is there a socket attached.
     * 
     * @return bool - true if there is o socket
     */
    bool empty() {return m_socket == -1;}

    /**
     * Sets the socket to be blocking or non-blocking. 
     * See O_NONBLOCK. 
     * @note Use GetLastError() for error code on failure. 
     */
    bool SetBlocking(bool block);


    /**
     * Sets whether port can be reused. 
     * See SO_REUSEADDR.
     * @note Use GetLastError() for error code on failure. 
     */
    bool SetReusePort(bool reuse);

    /**
     * Sets whether receive timestamp is included. 
     * See SO_TIMESTAMP. 
     * @note Use GetLastError() for error code on failure. 
     */
    bool SetUseTimestamp(bool timestamp);

    /**
     * Sets send buffer size. 
     * See SO_SNDBUF. 
     * @note Use GetLastError() for error code on failure. 
     */
    bool SetSendBufferSize(int bufsize);

    /**
     * Gets send buffer size. 
     * See SO_SNDBUF. 
     * @note Use GetLastError() for error code on failure. 
     */
    bool GetSendBufferSize(int &out_bufsize);

    /**
     * Sets socket option IP_TTL or IPV6_UNICAST_HOPS depending on the type.
     * @note Use GetLastError() for error code on failure. 
     */
    bool SetTTLOrHops(int hops);

    /**
     * Sets IP_RECVTTL, or IPV6_RECVHOPLIMIT depending on the type.
     * @note Use GetLastError() for error code on failure. 
     */
    bool SetRecieveTTLOrHops(bool receive);

    /**
     * @return size_t - Maximum needed control size when using SetRecieveTTLOrHops 
     */
    static size_t GetMaxControlSizeRecieveTTLOrHops();

    /**
     * Sets IPPROTO_IPV6::IPV6_V6ONLY
     * 
     * @note Use GetLastError() for error code on failure. 
     *  
     * @param ipv6Only 
     * 
     * @return bool 
     */
    bool SetIPv6Only(bool ipv6Only);


    /**
     * Will attempt to enable (or disable) receiving of destination packet address.
     * 
     * @note Use GetLastError() for error code on failure. 
     *  
     * @param receive 
     * 
     * @return bool 
     */
    bool SetReceiveDestinationAddress(bool receive);

    /**
     * @return size_t - Maximum needed control size when using SetRecieveTTLOrHops 
     */
    static size_t GetMaxControlSizeReceiveDestinationAddress();

    /** 
     * See socket bind().
     *  
     * @note Use GetLastError() for error code on failure. 
     *  
     * @return bool - False on failure
     */
    bool Bind(const SockAddr &address);

    /** 
     * See socket ::connect(). 
     *  
     * @note Use GetLastError() for error code on failure. 
     * 
     * @return bool - False on failure
     */
    bool Connect(const SockAddr &address);

    /**
     * See socket listen()
     *  
     * @note Use GetLastError() for error code on failure. 
     *  
     * @param backlog 
     * 
     * @return bool - False on failure.
     */
    bool Listen(int backlog);
    
    /**
     * Like sendto()
     *  
     * @note Use GetLastError() for error code on failure. 
     *  
     * @param buffer 
     * @param bufferLen 
     * @param toAddress 
     * @param flags 
     * 
     * @return bool - False on failure.
     */
    bool SendTo(const void *buffer, size_t bufferLen, const SockAddr &toAddress, int flags = 0);

    /**
     * Like send()
     *  
     * @note Use GetLastError() for error code on failure. 
     *  
     * @param buffer 
     * @param bufferLen 
     * @param flags 
     * 
     * @return bool - False on failure.
     */
    bool Send(const void *buffer, size_t bufferLen, int flags = 0);


    /**
     * See socket accept(). 
     * On success, use GetAddress() on source to check source address. 
     *  
     * outResult will be owned by default.
     *  
     * @note Use GetLastError() for error code on failure. 
     *  
     * 
     * @param outResult [out] -  On success, this is the socket. On failure, this is 
     *                  Closed.
     * 
     * @return bool 
     */
    bool Accept(Socket &outResult);

    /**
     * For a socket created with Bind or Connect(), this is the bound address. 
     * For sockets created from Accept, this is the source address. 
     *  
     * May be an invalid address if the socket was attached or is empty.
     * 
     * @return uint32_t 
     */
    const SockAddr &GetAddress() {return m_address;}
    
    /**
     * Auto conversion to int to allow it to be treated like a socket value.
     * 
     * 
     * @return int 
     */
    operator int() const  { return m_socket;}

    /** 
     * Copies socket.
     * The LogName and Quiet values are not copied.
     * 
     * @note The new socket is NOT owned.
     */
    Socket &operator=(const Socket& src);

  private:
    bool setIntSockOpt(int level, int optname, const char *name, int value);
    bool getIntSockOpt(int level, int optname, const char *name, int &out_value);

    void clear();
    void copy(const Socket &src);

    bool ensureSocket();
    bool logAndSetError(int error, const char* format, ...) ATTR_FORMAT(printf, 3, 4);
    bool logError(const char* format, ...) ATTR_FORMAT(printf, 2, 3);


    int m_socket;
    SockAddr m_address;
    int m_owned;  // Close when destroyed.
    int m_error;
    std::string m_logName;
    bool m_quiet;
  };
}
