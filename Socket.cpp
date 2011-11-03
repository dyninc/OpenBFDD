/************************************************************** 
* Copyright (c) 2011, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "Socket.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>

using namespace std;

namespace openbfdd
{

  void Socket::clear() 
  {
    m_socket = -1; 
    m_address.clear();
    m_owned = false;
  }

  Socket::Socket()
  { 
    clear();
  }

  Socket::Socket(int sock, Addr::Type family, bool owned /*false*/)
  {
    clear();
    m_socket = sock;
    m_address.SetAny(family);
    m_owned = owned;
  }

  Socket::Socket(const Socket &src)
  {
    clear();
    copy(src);
  }

  /**
   * Copies the socket. This will NOT own the socket. 
   * 
   * @param src 
   */
  void Socket::copy(const Socket &src)
  {
    Close();
    m_socket = src.m_socket;
    m_address = src.m_address;
  }

  Socket::~Socket()
  {
    Close();
  }

  Socket &Socket::operator=(const Socket& src)
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
    {
      gLog.LogError("Failed create socket. family %d, type %d proto %d : %s", family, type, protocol, strerror(errno));
      return false;
    }

    m_address.SetAny(family);
    m_owned = true;

    return true;
  }


  bool Socket::OpenUDP( Addr::Type family)
  {
    return Open(family, SOCK_DGRAM, IPPROTO_UDP);
  }

  bool Socket::OpenTCP( Addr::Type family)
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

  void Socket::Attach(int sock, const sockaddr *addr, socklen_t addrlen, bool owned /*false*/)
  {
    Close();
    m_socket = sock;
    m_address = SockAddr(addr, addrlen);
    m_owned = owned;
  }

	void Socket::Transfer(Socket &src) 
	{
    Close();
		if(src.m_owned)
		{
			copy(src.Detach());
			TakeOwnership();
		}
		else
			copy(src);
	}


  void Socket::Close()
  {
    if (!empty() && m_owned)
      ::close(m_socket);
    clear();  // must do full clear, because internally we rely on that behavior.
  }

  void Socket::AlwaysClose()
  {
    if (!empty())
      ::close(m_socket);
    clear();  // must do full clear, because internally we rely on that behavior.
  }


  bool Socket::SetBlocking(bool block)
  {
    if (empty())
      return false;

    int flags;
    flags = ::fcntl(m_socket, F_GETFL);
    if (flags == -1) 
    {
      gLog.LogError("Failed to get socket flags to set to %sblocking : %s", block ? "":"non-", strerror(errno));
      return false;
    }
    if (block)
      flags &= ~(int)O_NONBLOCK;
    else
      flags |= O_NONBLOCK;
    if (-1 == ::fcntl(m_socket, F_SETFL, flags))
    {
      gLog.LogError("Failed to set socket to %sblocking : %s", block ? "":"non-", strerror(errno));
      return false;
    }
    return true;
  }

  /**
   * Calls setsockopt.
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

    if (empty())
      return false;

    int on = value;

    if (0 > ::setsockopt(m_socket, level, optname, &on, sizeof(on)))
    {
      gLog.LogError("Failed to set socket %s to %d: %s", name, value, strerror(errno));
      return false;
    }
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

    if (empty())
      return false;

    socklen_t out_len = sizeof(int);  
    out_value = 0;

    if (0 > ::getsockopt(m_socket, level, optname, &out_value, &out_len))
    {
      gLog.LogError("Failed to get socket %s: %s", name, strerror(errno));
      return false;
    }
    return true;
  }


  bool Socket::SetReusePort(bool reuse)
  {
    return setIntSockOpt(SOL_SOCKET, SO_REUSEADDR, "SO_REUSEADDR", reuse ? 1:0);
  }

  bool Socket::SetSendBufferSize(int bufsize)
  {
    return setIntSockOpt(SOL_SOCKET, SO_SNDBUF, "SO_SNDBUF", bufsize);
  }

  bool Socket::GetSendBufferSize(int &out_bufsize)
  {
    return getIntSockOpt(SOL_SOCKET, SO_SNDBUF, "SO_SNDBUF", out_bufsize);
  }

  bool Socket::SetUseTimestamp(bool timestamp)
  {
    return setIntSockOpt(SOL_SOCKET, SO_TIMESTAMP, "SO_TIMESTAMP", timestamp ? 1:0);
  }



  bool Socket::SetTTLOrHops(int hops)
  {
    if (GetAddress().IsIPv4())
    {
      // Is there a 255 limit?
      return setIntSockOpt(IPPROTO_IP, IP_TTL, "IP_TTL", hops);
    }
    else if (GetAddress().IsIPv6())
    {   
      return setIntSockOpt(IPPROTO_IPV6, IPV6_UNICAST_HOPS, "IPV6_UNICAST_HOPS", hops);
    }
    else 
      return false;
  }

  bool Socket::SetRecieveTTLOrHops(bool receive)
  {
    int val = receive ? 1:0;

    if (GetAddress().IsIPv4())
    {
      // Is there a 255 limit?
      return setIntSockOpt(IPPROTO_IP, IP_RECVTTL, "IP_RECVTTL", val);
    }
    else if (GetAddress().IsIPv6())
    {   
      return setIntSockOpt(IPPROTO_IPV6, IPV6_RECVHOPLIMIT, "IPV6_RECVHOPLIMIT", val);
    }
    else 
      return false;
  }

  bool Socket::SetReceiveDestinationAddress(bool receive)
  {
    int val = receive ? 1:0;
    if (GetAddress().IsIPv4())
    {
    #ifdef IP_RECVDSTADDR
      return setIntSockOpt(IPPROTO_IP, IP_RECVDSTADDR, "IP_RECVDSTADDR", val);
    #elif defined IP_PKTINFO
      return setIntSockOpt(IPPROTO_IP, IP_PKTINFO, "IP_PKTINFO", val);
    #endif
      return false;
    }
    else if (GetAddress().IsIPv6())
    {   
      return setIntSockOpt(IPPROTO_IPV6, IPV6_RECVPKTINFO, "IPV6_RECVPKTINFO", val);
    }
    else 
      return false;
  }


  bool Socket::SetIPv6Only(bool ipv6Only)
  {
    if (GetAddress().IsIPv6())
    {
      return setIntSockOpt(IPPROTO_IPV6, IPV6_V6ONLY, "IPV6_V6ONLY", ipv6Only ? 1:0);
    }
    else
      return false;
  }

  bool Socket::Bind(const SockAddr &address)
  {
    if (empty())
      return false;

    m_address.clear();

    if (::bind(m_socket, &address.GetSockAddr(), address.GetSize()) < 0)
    {
      gLog.LogError("Failed to bind socket to %s : %s", address.ToString(), strerror(errno));
      return false;
    }

    m_address = address;
    return true;
  }



  bool Socket::Connect(const SockAddr &address)
  {
    if (empty())
      return false;

    m_address.clear();

    if (::connect(m_socket, &address.GetSockAddr(), address.GetSize()) < 0)
    {
      gLog.LogError("Failed to connect socket to %s : %s", address.ToString(), strerror(errno));
      return false;
    }

    m_address = address;
    return true;
  }

  bool Socket::Listen(int backlog)
  {
    if (empty())
      return false;

    if (::listen(m_socket, backlog) < 0)
    {
      gLog.LogError("Failed to listen on socket : %s",  strerror(errno));
      return false;
    }
    return true;
  }

	bool Socket::SendTo(const void *buffer, size_t bufferLen, const SockAddr &toAddress, int flags /*0*/)
	{
    if (empty())
      return false;

    if (::sendto(m_socket, buffer, bufferLen, flags, &toAddress.GetSockAddr(), toAddress.GetSize()) < 0)
    {
      gLog.LogError("Error sending packet using sendto : %s", strerror(errno));
      return false;
    }

		return true;
	}

	bool Socket::Send(const void *buffer, size_t bufferLen, int flags /*0*/)
	{
    if (empty())
      return false;

    if (::send(m_socket, buffer, bufferLen, flags) < 0)
    {
      gLog.LogError("Error sending packet using send : %s", strerror(errno));
      return false;
    }

		return true;
	}


  bool Socket::Accept(Socket &outResult)
  {
    if (empty())
      return false;

    outResult.Close();

    sockaddr_storage faddr;
    socklen_t fromlen = sizeof(faddr);
    int sock = ::accept(m_socket, (sockaddr*)&faddr, &fromlen);
    if (sock == -1)
    {
      gLog.LogError("Failed to accept on socket : %s",  strerror(errno));
      return false;
    }

    // It is always success, when if we can not read from addr
    outResult.m_socket = sock;
    outResult.m_address = SockAddr((sockaddr*)&faddr, fromlen);
    outResult.m_owned= true;

    if (!outResult.m_address.IsValid())
      gLog.LogError("Unexpected invalid address from accept. Size %zu", size_t(fromlen));

    return true;
  }

	size_t Socket::GetMaxControlSizeRecieveTTLOrHops()
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

	Socket::RecvMsg::RecvMsg() :
		m_controlBufferSize(0),
		m_dataBufferSize(0)
	{
		clear();
	}

	Socket::RecvMsg::RecvMsg(size_t bufferSize, size_t controlSize) :
	  m_controlBufferSize(0),
		m_dataBufferSize(0)

	{
		clear();
		AllocBuffers(bufferSize, controlSize); 
	}

	/**
	 * Clears everything except the buffers.
	 * 
	 */
	void Socket::RecvMsg::clear()
	{
		m_dataBufferValidSize = 0;
		m_sourceAddress.clear();
		m_destAddress.clear();
		m_ttlOrHops = -1;
		m_error = 0;
	}

	void Socket::RecvMsg::AllocBuffers(size_t bufferSize, size_t controlSize)
	{
		m_controlBuffer = new uint8_t[controlSize];
		m_controlBufferSize = controlSize;

		m_dataBuffer = new uint8_t[bufferSize];
		m_dataBufferSize = bufferSize;
		m_dataBufferValidSize = 0;
	}

	bool Socket::RecvMsg::DoRecvMsg(const Socket &socket)
	{

		if (m_dataBufferSize == 0)
		{
			m_error = EINVAL;
			return false;
		}

		clear();

    struct iovec msgiov;
    msgiov.iov_base = m_dataBuffer.val;
    msgiov.iov_len =  m_dataBufferSize;

    sockaddr_storage msgaddr;
    struct msghdr message; 
    message.msg_name =&msgaddr;
    message.msg_namelen = sizeof(msgaddr);
    message.msg_iov = &msgiov;
    message.msg_iovlen = 1;  // ??
    message.msg_control = m_controlBuffer;
    message.msg_controllen = m_controlBufferSize;
    message.msg_flags = 0;

    // Get packet
    ssize_t msgLength = recvmsg(socket, &message, 0);
    if (msgLength < 0)
    {
			m_error = errno;
      return false;
    }


		m_sourceAddress = SockAddr(reinterpret_cast<sockaddr *>(message.msg_name), message.msg_namelen);
		if (!m_sourceAddress.IsValid())
		{
			m_error = EILSEQ; //??
			return false;
		}

		// Walk control messages, and see what we can see.
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&message); cmsg != NULL; cmsg = CMSG_NXTHDR(&message, cmsg))
    {
      // It appears that  some systems use IP_TTL and some use IP_RECVTTL to
      // return the ttl. Specifically, FreeBSD uses IP_RECVTTL and Debian uses
      // IP_TTL. We work around this by checking both (until that breaks some system.)
      if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TTL)
      {
        if (LogVerify(cmsg->cmsg_len >= CMSG_LEN(sizeof(int))))
          m_ttlOrHops = (uint8_t)*(int *)CMSG_DATA(cmsg);
      }
      else if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_RECVTTL)
      {
        m_ttlOrHops = *(uint8_t *)CMSG_DATA(cmsg);
      }
      else if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_HOPLIMIT)
			{
        if (LogVerify(cmsg->cmsg_len >= CMSG_LEN(sizeof(int))))
          m_ttlOrHops = (uint8_t)*(int *)CMSG_DATA(cmsg);
			}
#ifdef IP_RECVDSTADDR
      else if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_RECVDSTADDR)
      {
        if (LogVerify(cmsg->cmsg_len >= CMSG_LEN(sizeof(in_addr))))
				  m_destAddress = IpAddr(reinterpret_cast<in_addr *>(CMSG_DATA(cmsg)));
      }
#endif
#ifdef IP_PKTINFO
      else if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO)
      {
        if (LogVerify(cmsg->cmsg_len >= CMSG_LEN(sizeof(in_pktinfo))))
						m_destAddress = IpAddr(&reinterpret_cast<in_pktinfo *>(CMSG_DATA(cmsg))->ipi_addr);
      }
#endif
      else if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO)
      {
        if (LogVerify(cmsg->cmsg_len >= CMSG_LEN(sizeof(in6_pktinfo))))
				{
					in6_pktinfo *info = reinterpret_cast<in6_pktinfo *>(CMSG_DATA(cmsg));
					m_destAddress = IpAddr(&info->ipi6_addr);
					if (info->ipi6_ifindex != 0)
						m_destAddress.SetScopIdIfLinkLocal(info->ipi6_ifindex);
				}
      }
    }

		m_dataBufferValidSize = size_t(msgLength);
		return true;
	}

	bool Socket::RecvMsg::DoRecv(const Socket &socket, int flags)
	{

		if (m_dataBufferSize == 0)
		{
			m_error = EINVAL;
			return false;
		}

		clear();

		ssize_t msgLength = recv(socket, m_dataBuffer,  (int)m_dataBufferSize, flags);
    if (msgLength < 0)
    {
			m_error = errno;
      return false;
    }

		m_dataBufferValidSize = size_t(msgLength);
		return true;
	}



	uint8_t Socket::RecvMsg::GetTTLorHops(bool *success)
	{
		if (m_ttlOrHops < 0 || m_ttlOrHops > 255)
		{
			if (success)
				*success = false;
			return 0;
		}
		if (success)
			*success = true;
		return (uint8_t)m_ttlOrHops;
	}


}
