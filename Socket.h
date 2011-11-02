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
     * Copies socket.
     * 
     * @note The new socket is NOT owned.
     */
    Socket(const Socket &src);


    ~Socket();

    /**
     * Attempts to create a socket. 
     * Will be owned by default. 
     * 
     * @param family [in] - The address family that the socket uses
     * @param type 
     * @param protocol 
     * 
     * @return bool 
     */
    bool Open(Addr::Type family, int type, int protocol);

    /**
     * Same as open, with (family, SOCK_DGRAM, IPPROTO_UDP)
     *  
     * @param family [in] - The address family that the socket uses
     *  
     * @return bool 
     */
    bool OpenUDP(Addr::Type family);

    /**
     * Same as open, with (family, SOCK_STREAM, IPPROTO_TCP)
     *  
     * @param family [in] - The address family that the socket uses
     *  
     * @return bool 
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
    void Attach(int sock, const struct sockaddr *addr, socklen_t addrlen, bool owned = false);


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
     */
    void Transfer(Socket &src);


    /**
     * Returns the socket.
     * 
     * @return int 
     */
    int GetSocket() {return m_socket;}

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
     */
    bool SetBlocking(bool block);


    /**
     * Sets whether port can be reused. 
     * See SO_REUSEADDR.
     */
    bool SetReusePort(bool reuse);

    /**
     * Sets whether receive timestamp is included. 
     * See SO_TIMESTAMP. 
     */
    bool SetUseTimestamp(bool timestamp);

    /**
     * Sets send buffer size. 
     * See SO_SNDBUF. 
     */
    bool SetSendBufferSize(int bufsize);

    /**
     * Gets send buffer size. 
     * See SO_SNDBUF. 
     */
    bool GetSendBufferSize(int &out_bufsize);

    /**
     * Sets socket option IP_TTL or IPV6_UNICAST_HOPS depending on the type.
     */
    bool SetTTLOrHops(int hops);

    /**
     * Sets IP_RECVTTL, or IPV6_RECVHOPLIMIT depending on the type.
     */
    bool SetRecieveTTLOrHops(bool receive);

    /**
     * @return size_t - Maximum needed control size when using SetRecieveTTLOrHops 
     */
    static size_t GetMaxControlSizeRecieveTTLOrHops();

    /**
     * Sets IPPROTO_IPV6::IPV6_V6ONLY
     * 
     * @param ipv6Only 
     * 
     * @return bool 
     */
    bool SetIPv6Only(bool ipv6Only);


    /**
     * Will attempt to enable (or disable) receiving of destination packet address.
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
     * @return bool - False on failure
     */
    bool Bind(const SockAddr &address);

    /** 
     * See socket ::connect().
     * 
     * @return bool - False on failure
     */
    bool Connect(const SockAddr &address);

    /**
     * See socket listen()
     * 
     * @param backlog 
     * 
     * @return bool - False on failure.
     */
    bool Listen(int backlog);
    
    /**
     * Like sendto()
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
     * 
     * @note The new socket is NOT owned.
     */
    Socket &operator=(const Socket& src);

  private:
    bool setIntSockOpt(int level, int optname, const char *name, int value);
    bool getIntSockOpt(int level, int optname, const char *name, int &out_value);

    void clear();
    void copy(const Socket &src);


    int m_socket;
    SockAddr m_address;
    int m_owned;  // Close when destroyed.


  public:
    /**
     * A container for recv or recvmsg results.
     */
    class RecvMsg
    {
    public:

      /**
       * Creates an empty RecvMsgData. 
       *  
       * Must call AllocBuffers() before calling RecvMsg(). 
       */
      RecvMsg();

      /**
       * Creates a RecvMsg and allocates storage. 
       *  
       * @throw - yes 
       * 
       * @param bufferSize [in] - The size of the buffer for receiving data. 
       *  
       * @param controlSize [in] - The size of the buffer for receiving control 
       *                   messages. This should be large enough for all enabled
       *                   messages. Use CMSG_SPACE to calculate desired size. May be
       *                   0 when used for recv.
       */
      RecvMsg(size_t bufferSize, size_t controlSize);

      /**
       * Allocates storage. 
       *  
       * @throw - yes 
       * 
       * @param bufferSize [in] - The size of the buffer for receiving data. 
       *  
       * @param controlSize [in] - The size of the buffer for receiving control 
       *                   messages. This should be large enough for all enabled
       *                   messages. Use CMSG_SPACE to calculate desired size. May be
       *                   0 when used for recv.
       */
      void AllocBuffers(size_t bufferSize, size_t controlSize);

      /**
       * Call recvmsg for the given socket. 
       * Call GetLastError() on failure to get the errno.
       * 
       * @param socket 
       * 
       * @return bool - false on failure. 
       */
      bool DoRecvMsg(const Socket &socket);

      /**
       * Call recv for the given socket. Call GetLastError() on failure to get the 
       * errno. 
       * 
       * @param socket 
       * @param flags - Flags for recv. 
       * 
       * @return bool - false on failure. 
       */
      bool DoRecv(const Socket &socket, int flags);

      /**
       * @return - The error from the last DoRecvMsg call. 0 if it succeeded. 
       */
      int GetLastError() {return m_error;}

      /**
       * Gets the TTL or Hops (for IPv6). 
       *  
       * 
       * @param success [out] - False on failure.
       * 
       * @return uint8_t - The ttl or hops. 0 on failure. (Note 0 is also a valid 
       *         value, use success to determine failure).
       */
      uint8_t GetTTLorHops(bool *success = NULL);

      /**
       * Gets the destination address.
       * 
       * @return IpAddr - IsValid() will return false on failure.
       */
      const IpAddr &GetDestAddress() {return m_destAddress;}

      /**
       * The source address.
       * 
       * @return SockAddr - IsValid() will return true on failure.
       */
      const SockAddr &GetSrcAddress() {return m_sourceAddress;}

      /**
       * Gets the data from the last DoRecvMsg(), if successful. 
       * 
       * 
       * @return - Data from the last DoRecvMsg(), if successful. NULL if DoRecvMsg 
       *         was never called, or it failed.
       */
      uint8_t *GetData() {return m_dataBufferValidSize ? m_dataBuffer.val:NULL;}

      /**
       * Gets the size of the data from the last DoRecvMsg(), if successful. 
       * 
       * 
       * @return - The size of valid data from the last DoRecvMsg(), if 
       *         successful. 0 if DoRecvMsg was never called, or it failed.
       */
      size_t GetDataSize() {return m_dataBufferValidSize;}

    private:
      void clear();
    private:
      Riaa<uint8_t>::DeleteArray m_controlBuffer; // Not using vector, because we do not want initialization.
      size_t m_controlBufferSize;
      Riaa<uint8_t>::DeleteArray m_dataBuffer; // Not using vector, because we do not want initialization.
      size_t m_dataBufferSize;
      size_t m_dataBufferValidSize;  // Only valid after successful DoRecvMsg 
      SockAddr m_sourceAddress;
      IpAddr m_destAddress;
      int16_t m_ttlOrHops; // -1 for invalid
      int m_error;
    };
  };
}
