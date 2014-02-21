/**************************************************************
* Copyright (c) 2011, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once

#include "common.h"
#include "SockAddr.h"
#include "SmartPointer.h"
#include <sys/socket.h>

/**
 * Class for socket. It can perform some basic socket tasks as well. All
 * functions will log to Log::Error on failure.
 * It has optional ::close() semantics.
 * Call UtilsInit() before using.
 * This can be an RAII class, but it has some 'specialized' ownership semantics
 * that allow it to be used in a container class. Care must be taken to ensure
 * that ownership is handled correctly when using this class.
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
  Socket& Detach() { m_owned = false; return *this;}


  /**
   * If this is a valid socket, then it will be owned. That is it will be closed
   * when the destructor is called, or Close() is called
   */
  void TakeOwnership() { m_owned = true;}


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
  const char* LogName() { return m_logName.c_str();}


  /**
   * Disables logging of errors.
   *
   * @param quiet
   *
   * @return bool - The old quiet value.
   */
  bool SetQuiet(bool quiet);

  /**
   * By default, certain 'expected' errors, such as EAGAIN and EINTR are logged at
   * the Log::Debug level. Setting verbose will log these at the level given. If
   * logging is disabled for this socket (see SetQuiet() ), then these messages
   * will not be logged.
   *
   * @param type [in] - The log level to use for logging these 'errors'. Use
   *             Log::TypeCount to disable logging of these.
   *
   * @return Log::Type - The old verbose value.
   */
  Log::Type SetVerbose(Log::Type type);

  /**
   * Returns the socket.
   *
   * @return int
   */
  int GetSocket() { return m_socket;}

  /**
   * @return - The error from the last call. 0 if it succeeded.
   * @note - only calls that specifically state in their comments that they set
   *       the error are guaranteed to do so. Others may, or may not.
   */
  int GetLastError() { return m_error;}

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
  bool empty() { return m_socket == -1;}

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
   * Sets receive buffer size.
   * See SO_RCVBUF.
   * @note Use GetLastError() for error code on failure.
   */
  bool SetReceiveBufferSize(int bufsize);

  /**
   * Gets receive buffer size.
   * See SO_RCVBUF.
   * @note Use GetLastError() for error code on failure.
   */
  bool GetReceiveBufferSize(int &out_bufsize);

  /**
   * Sets socket option IP_TTL or IPV6_UNICAST_HOPS depending on the type.
   * @note Use GetLastError() for error code on failure.
   */
  bool SetTTLOrHops(int hops);

  /**
   * Sets IP_RECVTTL, or IPV6_RECVHOPLIMIT depending on the type.
   * @note Use GetLastError() for error code on failure.
   */
  bool SetReceiveTTLOrHops(bool receive);

  /**
   * @return size_t - Maximum needed control size when using SetReceiveTTLOrHops
   */
  static size_t GetMaxControlSizeReceiveTTLOrHops();

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
   * @return size_t - Maximum needed control size when using SetReceiveTTLOrHops
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
   * Like sendto().
   *
   * @note - a 'partial send' is possible on stream based protocols, like TCP.
   * In that case, this function will return true, but not all data is sent. for
   * this reason, this is not reccomended for stream based protocols. Use
   * SendToStream() instead.
   *
   *
   * @note Use GetLastError() for error code on failure.
   *
   * @param buffer
   * @param bufferLen
   * @param toAddress
   * @param flags
   *
   * @return bool - False on failure. A partial write is NOT failure.
   */
  bool SendTo(const void *buffer, size_t bufferLen, const SockAddr &toAddress, int flags = 0);

  /**
   * Like sendto(), for stream based protocols.
   *
   * This function can have one of three results. If the write completes
   * successfully, and completely, then 'buffer' will be set to NULL, and
   * 'bufferLen' will be set to 0. If the write completes partially, then 'buffer'
   * and 'bufferLen' will be modified to reflect the unwritten data location and
   * length. In this case the function returns true. If there is some other
   * failure, then 'buffer' and 'bufferLen' will remain unchanged, and false will
   * be returned.
   *
   * @note Use GetLastError() for error code on failure.
   *
   * @param buffer [in/out] - The buffer containing the data to be written. See
   *         description for value on return.
   * @param bufferLen [in/out] - The number of bytes to write. See
   *         description for value on return.
   * @param toAddress
   * @param flags
   *
   * @return bool - False on failure. A partial write is NOT failure.
   */
  bool SendToStream(const void **buffer, size_t *bufferLen, const SockAddr &toAddress, int flags = 0);
  bool SendToStream(void **buffer, size_t *bufferLen, const SockAddr &toAddress, int flags = 0);

  /**
   * Like send().
   *
   * @note - a 'partial send' is possible on stream based protocols, like TCP.
   * In that case, this function will return true, but not all data is sent. for
   * this reason, this is not reccomended for stream based protocols. Use
   * SendStream,() instead.
   *
   * @note Use GetLastError() for error code on failure.
   *
   * @param buffer
   * @param bufferLen
   * @param flags
   *
   * @return bool - False on failure. A partial write is NOT failure.
   */
  bool Send(const void *buffer, size_t bufferLen, int flags = 0);


  /**
   * Like send(), for stream based protocols.
   *
   * This function can have one of three results. If the write completes
   * successfully, and completely, then 'buffer' will be set to NULL, and
   * 'bufferLen' will be set to 0. If the write completes partially, then 'buffer'
   * and 'bufferLen' will be modified to reflect the unwritten data location and
   * length. In this case the function returns true. If there is some other
   * failure, then 'buffer' and 'bufferLen' will remain unchanged, and false will
   * be returned.
   *
   * @note Use GetLastError() for error code on failure.
   *
   * @param buffer [in/out] - The buffer containing the data to be written. See
   *         description for value on return.
   * @param bufferLen [in/out] - The number of bytes to write. See
   *         description for value on return.
   *
   * @return bool - False on failure. A partial write is NOT failure.
   */
  bool SendStream(const void **buffer, size_t *bufferLen, int flags = 0);
  bool SendStream(void **buffer, size_t *bufferLen, int flags = 0);

  /**
   * After a call to SendTo(), SendToStream(), Send() or SendStream() returns
   * false, this will check if the error was fatal, or if it is reasonable to try
   * sending again.
   *
   *
   * @return bool - false if it may be reasonable to try sending again.
   */
  bool LastErrorWasSendFatal();

  /**
   * Like recv()
   *
   * @param buffer [out] - Filled with received data on success.
   * @param inOutBufferLen [in/out] - Should be to size of buffer on calling. Will
   *                       be set to the bytes received on return. Set to 0 on
   *                       error.
   * @param flags
   *
   * @return bool - False on error.
   */
  bool Receive(void *buffer, size_t *inOutBufferLen, int flags = 0);

  /**
   * Like recv(). This is like Receive(), but may be easier to use when
   * programming based protocols like TCP.
   *
   * @param buffer [in/out] - The buffer to hold the data. On success, this points
   *         to the next unused buffer position. May point to one past the
   *         end of the buffer if bufferRemain is 0 on return. Not changed
   *         on failure.
   * @param bufferRemain [in/out] - The available size of buffer. On return this
   *             will be the new available size of the new buffer value.
   * @param written [in/out] - This value will be incremented by the number of
   *          bytes written. This may be NULL. This mirrors bufferRemain,
   *          and is provided for convenance.
   * @param flags
   *
   * @return bool - False on failure.
   */
  bool ReceiveStream(void **buffer, size_t *bufferRemain, size_t *written = NULL, int flags = 0);

  /**
   * After a call to Receive(), or ReceiveStream() returns
   * false, this will check if the error was fatal, or if it is reasonable to try
   * receiving again.
   *
   * @return bool - false if it may be reasonable to try receiving again.
   */
  bool LastErrorWasReceiveFatal();

  /**
   * See socket accept().
   * On success, use GetAddress() on source to check source address.
   *
   * outResult will be owned by default.
   *
   * @note Use GetLastError() for error code on failure.
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
  const SockAddr& GetAddress() { return m_address;}

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
  Socket &operator=(const Socket &src);

private:
  bool setIntSockOpt(int level, int optname, const char *name, int value);
  bool getIntSockOpt(int level, int optname, const char *name, int &out_value);

  void clear();
  void copy(const Socket &src);

  bool ensureSocket();
  void doErrorLog(Log::Type type, const char *format, va_list args);
  bool setErrorAndLog(int error, const char *format, ...) ATTR_FORMAT(printf, 3, 4);
  bool setErrorAndLogAsExpected(bool isExpected, int error, const char* format, ...)ATTR_FORMAT(printf, 4, 5);
  bool logError(const char* format, ...)ATTR_FORMAT(printf, 2, 3);


  int m_socket;
  SockAddr m_address;
  int m_owned;    // Close when destroyed.
  int m_error;
  std::string m_logName;
  bool m_quiet;
  Log::Type m_verboseLogType;
};
