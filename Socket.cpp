/**************************************************************
* Copyright (c) 2011-2017, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "Socket.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

/**
 * Checks for 'expected', non-fatal errors that may occur in certain functions.
 * For now, the same set is used for all checks.
 */
static inline bool isErrorExpected(int error)
{
  return (error == ENOBUFS
          || error == EAGAIN
          || error == EINTR
          || error == ENOMEM
          || error == EWOULDBLOCK
         );
}

void Socket::clear()
{
  m_socket = -1;
  m_address.clear();
  m_owned = false;
  m_error = 0;
}

Socket::Socket() :
   m_logName()
   , m_quiet(false)
   , m_verboseErrorLogType(Log::Error)
   , m_verboseExpectedLogType(Log::Debug)

{
  clear();
}

Socket::Socket(int sock, Addr::Type family, bool owned /*false*/) :
   m_logName()
   , m_quiet(false)
   , m_verboseErrorLogType(Log::Error)
   , m_verboseExpectedLogType(Log::Debug)
{
  clear();
  m_socket = sock;
  m_address.SetAny(family);
  m_owned = owned;
}

Socket::Socket(const Socket &src) :
   m_logName()
   , m_quiet(false)
   , m_verboseErrorLogType(Log::Error)
   , m_verboseExpectedLogType(Log::Debug)
{
  clear();
  copy(src);
}

/**
 * Copies the socket. This will NOT own the socket.
 * Does not copy quiet, log or log name settings.
 *
 * @param src
 */
void Socket::copy(const Socket &src)
{
  Close();
  m_socket = src.m_socket;
  m_address = src.m_address;
  m_error = src.m_error;
}

Socket::~Socket()
{
  Close();
}

Socket& Socket::operator = (const Socket &src)
{
  Close();
  copy(src);
  return *this;
}


bool Socket::Open(Addr::Type family, int type, int protocol)
{
  Close();
  m_socket = ::socket(Addr::TypeToFamily(family), type, protocol);
  if (empty())
    return setErrorAndLog(errno, "Failed create socket. family %d, type %d proto %d", family, type, protocol);

  m_address.SetAny(family);
  m_owned = true;

  return true;
}


bool Socket::OpenUDP(Addr::Type family)
{
  return Open(family, SOCK_DGRAM, IPPROTO_UDP);
}

bool Socket::OpenTCP(Addr::Type family)
{
  return Open(family, SOCK_STREAM, IPPROTO_TCP);
}

void Socket::Attach(int sock,  Addr::Type family, bool owned /*false*/)
{
  Close();
  m_socket = sock;
  m_address.SetAny(family);
  m_owned = owned;
}

void Socket::Attach(int sock, const sockaddr *addr, socklen_t addrlen, bool owned	/*false*/)
{
  Close();
  m_socket = sock;
  m_address = SockAddr(addr, addrlen);
  m_owned = owned;
}

void Socket::Transfer(Socket &src)
{
  Close();
  if (src.m_owned)
  {
    copy(src.Detach());
    TakeOwnership();
  }
  else
    copy(src);
}

void Socket::SetLogName(const char *str)
{
  if (!str || !*str)
    m_logName.clear();
  else
    m_logName	= str;
}

bool Socket::SetQuiet(bool quiet)
{
  bool old = m_quiet;
  m_quiet = quiet;
  return old;
}


Log::Type Socket::SetVerbosity(Log::Type type)
{
  Log::Type old = m_verboseErrorLogType;
  m_verboseErrorLogType = type;
  return old;
}


Log::Type Socket::SetExpectedVerbosity(Log::Type type)
{
  Log::Type old = m_verboseExpectedLogType;
  m_verboseExpectedLogType = type;
  return old;
}


void Socket::Close()
{
  if (!empty() && m_owned)
    ::close(m_socket);
  clear();    // must do full clear, because internally we rely on that behavior.
}

void Socket::AlwaysClose()
{
  if (!empty())
    ::close(m_socket);
  clear();    // must do full clear, because internally we rely on that behavior.
}


bool Socket::SetBlocking(bool block)
{
  if (!ensureSocket())
    return false;

  int flags;
  flags = ::fcntl(m_socket, F_GETFL);
  if (flags == -1)
    return setErrorAndLog(errno, "Failed to get socket flags to set to %sblocking", block ? "" : "non-");

  if (block)
    flags &= ~(int)O_NONBLOCK;
  else
    flags	|= O_NONBLOCK;
  if (-1 == ::fcntl(m_socket, F_SETFL, flags))
    return setErrorAndLog(errno, "Failed to set socket to %sblocking", block ? "" : "non-");
  return true;
}

/**
 * Calls setsockopt.
 *
 * will set m_error
 *
 * @param level
 * @param optname
 * @param name
 * @param value
 *
 * @return bool
 */
bool Socket::setIntSockOpt(int level, int optname, const char *name, int value)
{

  if (!ensureSocket())
    return false;

  int on = value;

  if (0 > ::setsockopt(m_socket, level, optname, &on, sizeof(on)))
    return setErrorAndLog(errno, "Failed to set socket %s to %d",  name, value);
  return true;
}

/**
 * Calls getsockopt.
 *
 * @param level
 * @param optname
 * @param name
 * @param value [out] - set on success
 *
 * @return bool
 */
bool Socket::getIntSockOpt(int level, int optname, const char *name, int &out_value)
{

  if (!ensureSocket())
    return false;

  socklen_t out_len = sizeof(int);
  out_value = 0;

  if (0 > ::getsockopt(m_socket, level, optname, &out_value, &out_len))
    return setErrorAndLog(errno, "Failed to get socket %s",  name);
  return true;
}


bool Socket::SetReusePort(bool reuse)
{
  return setIntSockOpt(SOL_SOCKET, SO_REUSEADDR, "SO_REUSEADDR", reuse ? 1 : 0);
}

bool Socket::SetSendBufferSize(int bufsize)
{
  return setIntSockOpt(SOL_SOCKET, SO_SNDBUF, "SO_SNDBUF", bufsize);
}

bool Socket::GetSendBufferSize(int &out_bufsize)
{
  return getIntSockOpt(SOL_SOCKET, SO_SNDBUF, "SO_SNDBUF", out_bufsize);
}

bool Socket::SetReceiveBufferSize(int bufsize)
{
  return setIntSockOpt(SOL_SOCKET, SO_RCVBUF, "SO_RCVBUF", bufsize);
}

bool Socket::GetReceiveBufferSize(int &out_bufsize)
{
  return getIntSockOpt(SOL_SOCKET, SO_RCVBUF, "SO_RCVBUF", out_bufsize);
}

bool Socket::SetUseTimestamp(bool timestamp)
{
  return setIntSockOpt(SOL_SOCKET, SO_TIMESTAMP, "SO_TIMESTAMP", timestamp ? 1 : 0);
}

bool Socket::SetTTLOrHops(int hops)
{
  if (!ensureSocket())
    return false;

  if (GetAddress().IsIPv4())
  {
    // Is there a 255 limit?
    return setIntSockOpt(IPPROTO_IP, IP_TTL, "IP_TTL", hops);
  }
  else
    return setIntSockOpt(IPPROTO_IPV6, IPV6_UNICAST_HOPS, "IPV6_UNICAST_HOPS", hops);
}

bool Socket::SetReceiveTTLOrHops(bool receive)
{
  int val = receive ? 1 : 0;

  if (!ensureSocket())
    return false;
  else if (GetAddress().IsIPv4())
  {
    // Is there a 255 limit?
    return setIntSockOpt(IPPROTO_IP, IP_RECVTTL, "IP_RECVTTL", val);
  }
  else
    return setIntSockOpt(IPPROTO_IPV6, IPV6_RECVHOPLIMIT, "IPV6_RECVHOPLIMIT", val);
}

bool Socket::SetReceiveDestinationAddress(bool receive)
{
  int val = receive ? 1 : 0;
  if (!ensureSocket())
    return false;
  else if (GetAddress().IsIPv4())
  {
#ifdef IP_RECVDSTADDR
    return setIntSockOpt(IPPROTO_IP, IP_RECVDSTADDR, "IP_RECVDSTADDR", val);
#elif defined IP_PKTINFO
    return setIntSockOpt(IPPROTO_IP, IP_PKTINFO, "IP_PKTINFO", val);
#endif
    return setErrorAndLog(ENOTSUP, "Platform does not support IP_RECVDSTADDR or IP_PKTINFO");
  }
  else
    return setIntSockOpt(IPPROTO_IPV6, IPV6_RECVPKTINFO, "IPV6_RECVPKTINFO", val);
}


bool Socket::GetPendingError(int &ouError)
{
  return getIntSockOpt(SOL_SOCKET, SO_ERROR, "SO_ERROR", ouError);
}

bool Socket::SetIPv6Only(bool ipv6Only)
{
  if (!ensureSocket())
    return false;

  if (GetAddress().IsIPv6())
    return setIntSockOpt(IPPROTO_IPV6, IPV6_V6ONLY, "IPV6_V6ONLY", ipv6Only ? 1 : 0);
  else
    return setErrorAndLog(ENOTSUP, "IPV6_V6ONLY not supported on IPv4 socket");
}

bool Socket::Bind(const SockAddr &address)
{
  if (!ensureSocket())
    return false;

  m_address.clear();

  if (::bind(m_socket, &address.GetSockAddr(), address.GetSize()) < 0)
    return setErrorAndLog(errno, "Failed to bind socket to %s",  address.ToString());

  m_address = address;
  return true;
}


bool Socket::Connect(const SockAddr &address)
{
  if (!ensureSocket())
    return false;

  m_address.clear();

  if (::connect(m_socket, &address.GetSockAddr(), address.GetSize()) < 0)
  {
    bool expected = (errno == EINPROGRESS);
    setErrorAndLogAsExpected(expected, errno, "Failed to connect socket to %s",  address.ToString());
    if (!expected)
      return false;
  }

  m_address = address;
  return true;
}

bool Socket::Listen(int backlog)
{
  if (!ensureSocket())
    return false;

  if (::listen(m_socket, backlog) < 0)
    return setErrorAndLog(errno, "Failed to listen on socket");
  return true;
}

bool Socket::SendTo(const void *buffer, size_t bufferLen, const SockAddr &toAddress, int flags /*0*/)
{
  if (!ensureSocket())
    return false;

  if (::sendto(m_socket, buffer, bufferLen, flags, &toAddress.GetSockAddr(), toAddress.GetSize()) < 0)
    return setErrorAndLogAsExpected(isErrorExpected(errno), errno, "Error sending packet using sendto to %s", toAddress.ToString());

  return true;
}



bool Socket::SendToStream(const void **buffer, size_t *bufferLen, const SockAddr &toAddress, int flags /*0*/)
{
  if (!ensureSocket())
    return false;

  ssize_t result = ::sendto(m_socket, *buffer, *bufferLen, flags, &toAddress.GetSockAddr(), toAddress.GetSize());
  if (result < 0)
    return setErrorAndLogAsExpected(isErrorExpected(errno), errno, "Error sending packet using sendto");
  if (result == ssize_t(*bufferLen))
  {
    *bufferLen = 0;
    *buffer = NULL;
  }
  else
  {
    if (size_t(result) > *bufferLen)
    {
      *bufferLen = 0;
      *buffer = NULL;
      return logError("Unexpected sendto() sent more data than was supplied");
    }
    *buffer = reinterpret_cast<const char *>(*buffer) + result;
    *bufferLen -= result;
  }
  return true;
}

bool Socket::SendToStream(void **buffer, size_t *bufferLen, const SockAddr &toAddress, int flags /*0*/)
{
  return SendToStream(const_cast<const void **>(buffer), bufferLen, toAddress, flags);
}




bool Socket::Send(const void *buffer, size_t bufferLen, int flags /*0*/)
{
  if (!ensureSocket())
    return false;

  if (::send(m_socket, buffer, bufferLen, flags) < 0)
    return setErrorAndLogAsExpected(isErrorExpected(errno), errno, "Error sending packet using send");

  return true;
}




bool Socket::SendStream(const void **buffer, size_t *bufferLen, int flags /*0*/)
{
  if (!ensureSocket())
    return false;

  ssize_t result = ::send(m_socket, *buffer, *bufferLen, flags);
  if (result < 0)
    return setErrorAndLogAsExpected(isErrorExpected(errno), errno, "Error sending packet using send");
  if (result == ssize_t(*bufferLen))
  {
    *bufferLen = 0;
    *buffer = NULL;
  }
  else
  {
    if (size_t(result) > *bufferLen)
    {
      *bufferLen = 0;
      *buffer = NULL;
      return logError("Unexpected send() sent more data than was supplied");
    }
    *buffer = reinterpret_cast<const char *>(*buffer) + result;
    *bufferLen -= result;
  }
  return true;
}

bool Socket::SendStream(void **buffer, size_t *bufferLen, int flags /*0*/)
{
  return SendStream(const_cast<const void **>(buffer), bufferLen, flags);
}


/**
 * Like sendmsg(), for stream based protocols.
 *
 * This function can have one of three results:
 *
 *   1 - If the write completes successfully, and completely, then
 * message->msg_iov is set to NULL and message->msg_iovlen is set to 0. true is
 * returned.
 *   2 -  If the write completes partially, message->msg_iov and
 * message->msg_iovlen will be modified to reflect the unwritten data location
 * and length. In this case the function returns true.
 *   3 - If there is some other failure, then  message->msg_iov and
 * message->msg_iovlen will remain unchanged, and false will be returned.
 *
 * @note Use GetLastError() for error code on failure. LastErrorWasSendFatal()
 *  	 may be helpful when deciding whether to try again.
 *
 * @param message [in/out] - The message containing the data to be written.
 *  			 May no be NULL. See description for value on return.
 *
 * @return bool - False on failure. A partial write is NOT failure.
 */
bool Socket::SendMsgStream(struct msghdr *message, int flags /*0*/)
{
  if (!ensureSocket())
    return false;

  if (message->msg_iovlen == 0)
    return true;

  ssize_t result = ::sendmsg(m_socket, message, flags);
  if (result < 0)
    return setErrorAndLogAsExpected(isErrorExpected(errno), errno, "Error sending packet using sendmsg");

  // Determine how much has been written (Note - Some non-POSIX compliant systems
  // have message->msg_iovlen as size_t)
  int whichIOVec;
  for (whichIOVec = 0; whichIOVec < ssize_t(message->msg_iovlen); ++whichIOVec)
  {
    ssize_t thisLen = message->msg_iov[whichIOVec].iov_len;
    if (thisLen != 0 && result == thisLen)
    {
      // Completed this one
      ++whichIOVec;
      result -= thisLen;
      break;
    }
    else if (result < thisLen)
    {
      //  Partial write to here
      message->msg_iov[whichIOVec].iov_len = thisLen - result;
      message->msg_iov[whichIOVec].iov_base = reinterpret_cast<uint8_t *>(message->msg_iov[whichIOVec].iov_base) + result;
      result -= thisLen;
      break;
    }
    result -= thisLen;
  }
  // whichIOVec is one past the last complete write

  if (result != 0)
  {
    message->msg_iovlen = 0;
    message->msg_iov = NULL;
    return setErrorAndLog(EINVAL, "Unexpected sendmsg() sent more data than was supplied");
  }

  if (whichIOVec >= ssize_t(message->msg_iovlen))
  {
    // Full write
    message->msg_iovlen = 0;
    message->msg_iov = NULL;
    return true;
  }
  // partial write
  message->msg_iovlen -= whichIOVec;
  message->msg_iov += whichIOVec;
  return true;
}

bool Socket::LastErrorWasSendFatal()
{
  return !isErrorExpected(m_error);
}


bool Socket::Receive(void *buffer, size_t *inOutBufferLen, int flags /*0*/)
{
  if (!LogVerify(inOutBufferLen))
    return false;

  size_t bufLen = *inOutBufferLen;
  *inOutBufferLen = 0;

  if (!LogVerify(buffer))
    return false;

  if (!ensureSocket())
    return false;

  ssize_t size = ::recv(m_socket, buffer, bufLen, flags);
  if (size < 0)
    return setErrorAndLogAsExpected(isErrorExpected(errno), errno, "Error reading packet using recv");

  *inOutBufferLen = size_t(size);

  return true;
}


bool Socket::ReceiveStream(void **buffer, size_t *bufferRemain, size_t *written /*NULL*/, int flags /*0*/)
{
  if (!LogVerify(*bufferRemain))
    return false;
  if (!LogVerify(*buffer))
    return false;
  if (!ensureSocket())
    return false;

  size_t bufLen = *bufferRemain;

  ssize_t size = ::recv(m_socket, *buffer, bufLen, flags);
  if (size < 0)
    return setErrorAndLogAsExpected(isErrorExpected(errno), errno, "Error reading packet using recv");

  *buffer = reinterpret_cast<char *>(*buffer) + size;
  *bufferRemain -= size;
  if (written)
    *written += size;

  return true;
}

bool Socket::LastErrorWasReceiveFatal()
{
  return !isErrorExpected(m_error);
}


bool Socket::Accept(Socket &outResult)
{
  if (!ensureSocket())
    return false;

  outResult.Close();

  sockaddr_storage faddr;
  socklen_t fromlen = sizeof(faddr);
  int sock = ::accept(m_socket, (sockaddr *)&faddr, &fromlen);
  if (sock == -1)
    return setErrorAndLog(errno, "Failed to accept on socket");

  // It is always success, when if we can not read from addr
  outResult.m_socket = sock;
  outResult.m_address = SockAddr((sockaddr *)&faddr, fromlen);
  outResult.m_owned = true;

  if (!outResult.m_address.IsValid())
    gLog.LogError("Unexpected invalid address from accept. Size %zu", size_t(fromlen));

  return true;
}


size_t Socket::GetMaxControlSizeReceiveTTLOrHops()
{
  // IP_TTL and IPV6_HOPLIMIT use int, and IP_RECVTTL may use byte.
  return (CMSG_SPACE(int));

}

size_t Socket::GetMaxControlSizeReceiveDestinationAddress()
{
  // We could assume that in6_pktinfo is going to be the largest, but the
  // following actually models what we do.
  size_t size = 0;
  size = max(size, CMSG_SPACE(sizeof(in6_pktinfo)));
#ifdef IP_RECVDSTADDR
  size = max(size, CMSG_SPACE(sizeof(in_addr)));
#endif
#ifdef IP_PKTINFO
  size = max(size, CMSG_SPACE(sizeof(in6_pktinfo)));
#endif
  return size;
}

/**
 * Same as !empty(), but sets m_error and logs a message on failure.
 *
 * @return bool
 */
bool Socket::ensureSocket()
{
  if (!empty())
    return true;
  return setErrorAndLog(EBADF, "Socket is invalid");
}


inline bool Socket::shouldLog(bool expected)
{
  if (expected)
    return !m_quiet && m_verboseExpectedLogType != Log::TypeCount && gLog.LogTypeEnabled(m_verboseExpectedLogType);
  else
    return !m_quiet && m_verboseErrorLogType != Log::TypeCount && gLog.LogTypeEnabled(m_verboseErrorLogType);
}

/**
 * Helper function, will log a message based on m_error, and logs the message.
 * Will prepend the socket name, if any. Will append the error string.
 *
 * Will always log, so call shouldLog() first.
 *
 * @return - false always!
 */
void Socket::doErrorLog(Log::Type type, const char *format, va_list args)
{
  if (m_logName.empty() &&  m_error == 0)
  {
    // Special case to avoid extra copy
    gLog.MessageVa(type, format, args);
  }

  const char *str = FormatMediumStrVa(format, args);

  if (m_error != 0)
  {
    if (!m_logName.empty())
      gLog.Optional(type, "%s : %s : (%d) %s", m_logName.c_str(), str, m_error, SystemErrorToString(m_error));
    else
      gLog.Optional(type, "%s : (%d) %s", str, m_error, SystemErrorToString(m_error));
  }
  else
  {
    LogAssert(!m_logName.empty());  // the empty case handled at the start of this fn.
    gLog.Optional(type, "%s : %s", m_logName.c_str(), str);
  }

}


/**
 * Sets m_error to error, and logs the message, if settings allow.
 * Will prepend the socket name, if any. Will append the error string.
 * Will log 'expected' messages as prescribed by  SetVerbose().
 *
 * @param isExpected;
 * @param error
 * @param format
 *
 * @return - false always!
 */
bool Socket::setErrorAndLogAsExpected(bool isExpected, int error, const char *format, ...)
{
  m_error = error;

  if (!shouldLog(isExpected))
    return false;

  va_list args;
  va_start(args, format);
  doErrorLog(isExpected ? m_verboseExpectedLogType : m_verboseErrorLogType, format, args);
  va_end(args);
  return false;
}


/**
 * Sets m_error to error, and logs the message, if settings allow.
 * Will prepend the socket name, if any. Will append the error string.
 *
 * @param error
 * @param format
 *
 * @return - false always!
 */
bool Socket::setErrorAndLog(int error, const char *format, ...)
{
  m_error = error;

  if (!shouldLog(false))
    return false;

  va_list args;
  va_start(args, format);
  doErrorLog(m_verboseErrorLogType, format, args);
  va_end(args);
  return false;
}

/**
 * Logs the message, if settings allow. Will prepend the socket name, if any.
 * m_error will be cleared.
 *
 * @param error
 * @param format
 *
 * @return - false always!
 */
bool Socket::logError(const char *format, ...)
{
  m_error = 0;

  if (!shouldLog(false))
    return false;

  va_list args;
  va_start(args, format);
  doErrorLog(m_verboseErrorLogType, format, args);
  va_end(args);
  return false;
}
