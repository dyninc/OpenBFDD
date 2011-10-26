/************************************************************** 
* Copyright (c) 2011, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once

namespace openbfdd
{

  /**
   * RIAA class for socket, that can perform some basic socket tasks as well. 
   * Only supports IPv4 PF_INET sockets for now. 
   * All functions will log to Log::Error on failure. 
   */
  class Socket
  {
  public:

    Socket();
    /** This is explicit to avoid  it being a copy constructor. */
    explicit Socket(int sock);
    ~Socket();

    /**
     * Attempts to create a socket
     * 
     * @param type 
     * @param protocol 
     * 
     * @return bool 
     */
    bool Open(int type, int protocol);

    /**
     * Same as open, with SOCK_DGRAM, IPPROTO_UDP
     * 
     * @return bool 
     */
    bool OpenUDP();

    /**
     * Same as open, with SOCK_STREAM, IPPROTO_TCP
     * 
     * @return bool 
     */
    bool OpenTCP();

    /**
     * Attach an existing socket.
     * 
     * @param sock 
     */
    void Attach(int sock);

    /** 
     * Detach and return the socket. Will no longer be r esposible for closing the 
     * socket. 
     * 
     * @return int 
     */
    int Detach() {int sock = m_socket; clear(); return sock;}

    /**
     * Returns the socket.
     * 
     * @return int 
     */
    int GetSocket() {return m_socket;}

    /** 
     * Closes the socket
     * 
     */
    void Close();

    /**
     * Is there a socket attached.
     * 
     * @return bool 
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
     * See socket bind().
     * 
     * @param addr - Ipv4
     * @param port 
     * 
     * @return bool - False on failure
     */
    bool Bind(uint32_t addr, uint16_t port);

    /** 
     * See socket ::connect().
     * 
     * @param addr - Ipv4
     * @param port 
     * 
     * @return bool - False on failure
     */
    bool Connect(uint32_t addr, uint16_t port);

    /**
     * See socket listen()
     * 
     * @param backlog 
     * 
     * @return bool - False on failure.
     */
    bool Listen(int backlog);
    
    /**
     * See socket accept(). 
     * On success, use GetAddress() and GetPort() on source to check source address.
     *  
     * 
     * @param outResult [out] -  On success, this is the socket. On failure, this is 
     *                  Closed.
     * 
     * @return bool 
     */
    bool Accept(Socket &outResult);

    /**
     * For a socket created with Bind, Connect(), or Open, this is the bound 
     * address. For sockets created from Accept, this is the source address. 
     * May be INADDR_NONE if the socket was attached or is empty. 
     * 
     * @return uint32_t 
     */
    uint32_t GetAddress() {return m_addr;}
    
    /**
     * For a socket created with Bind, Connect, or Open, this is the bound port. For
     * sockets created from Accept, this is the source port. 
     * May be 0 if the socket was attached or is empty. 
     * 
     * @return uint16_t 
     */
    uint16_t GetPort() {return m_port;}

    /**
     * Auto conversion to int to allow it to be treated like a socket value.
     * 
     * 
     * @return int 
     */
    operator int() const  { return m_socket;}

  private:
    // No copy!
    Socket(const Socket &src);
    Socket &operator=(const Socket& src);

    bool setIntSockOpt(int level, int optname, const char *name, int value);
    bool getIntSockOpt(int level, int optname, const char *name, int &out_value);

    void clear() {m_socket = -1; m_addr = 0; m_port = 0;}

    int m_socket;
    uint32_t m_addr;
    uint16_t m_port;
  };
}
