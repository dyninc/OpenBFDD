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

  Socket::Socket()
  { 
    clear();
  }

  Socket::Socket(int sock)
  {
    clear();
    m_socket = sock;
  }


  Socket::~Socket()
  {
    Close();
  }


  bool Socket::Open(int type, int protocol)
  {
    Close();
    m_socket = ::socket(PF_INET, type, protocol);
    if (empty())
    {
      gLog.LogError("Failed create socket. type %d proto %d : %s", type, protocol, strerror(errno));
      return false;
    }
    return true;
  }


  bool Socket::OpenUDP()
  {
    return Open( SOCK_DGRAM, IPPROTO_UDP);
  }

  bool Socket::OpenTCP()
  {
    return Open( SOCK_STREAM, IPPROTO_TCP);
  }

  void Socket::Attach(int sock)
  {
    Close();
    m_socket = sock;
  }

  void Socket::Close()
  {
    if (!empty())
      ::close(m_socket);
    clear();  // must do full clear, becuase internally we rely on that behavior.
  }

  bool Socket::SetBlocking(bool block)
  {
    if (empty())
      return false;

    int flags;
    flags = ::fcntl(m_socket, F_GETFL);
    if (flags == -1) //?? 
    {
      gLog.LogError("Failed to get socket floags to set to %sblocking : %s", block ? "":"non-", strerror(errno));
      return false;
    }
    if (block)
      flags = flags & ~(int)O_NONBLOCK;
    else
      flags = flags | O_NONBLOCK;
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


  bool Socket::Bind(uint32_t addr, uint16_t port)
  {
    if (empty())
      return false;

    m_addr = 0;
    m_port = 0;

    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin)); 
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = addr;
    sin.sin_port = htons(port);

    if (::bind(m_socket, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
      gLog.LogError("Failed to bind socket to %s : %s", Ip4ToString(addr,  port), strerror(errno));
      return false;
    }

    m_addr = addr;
    m_port = port;
    return true;
  }



  bool Socket::Connect(uint32_t addr, uint16_t port)
  {
    if (empty())
      return false;

    m_addr = 0;
    m_port = 0;

    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin)); 
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = addr;
    sin.sin_port = htons(port);

    if (::connect(m_socket, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
      gLog.LogError("Failed to connect socket to %s : %s", Ip4ToString(addr,  port), strerror(errno));
      return false;
    }

    m_addr = addr;
    m_port = port;
    return true;
  }



  bool Socket::Listen(int backlog)
  {
    if (empty())
      return false;

    if (listen(m_socket, backlog) < 0)
    {
      gLog.LogError("Failed to listen on socket : %s",  strerror(errno));
      return false;
    }
    return true;
  }

  bool Socket::Accept(Socket &outResult)
  {
    if (empty())
      return false;

    outResult.Close();

    struct sockaddr_in faddr ; 
    socklen_t fromlen = sizeof(faddr);
    int sock = ::accept(m_socket, (sockaddr*)&faddr, &fromlen);
    if (sock == -1)
    {
      gLog.LogError("Failed to accept on socket : %s",  strerror(errno));
      return false;
    }

    // It is always sucess, wven if we can not read from addr
    outResult.m_socket = sock;

    if (fromlen < offsetof(sockaddr_in, sin_addr) + sizeof(faddr.sin_addr))
      gLog.LogError("Unexpected address size %zu from accept.", size_t(fromlen));
    else
    {
      if (faddr.sin_family != AF_INET)
        gLog.LogError("Unexpected address family %zu from accept.", size_t(faddr.sin_family));
      else
      {
        outResult.m_addr = faddr .sin_addr.s_addr;
        outResult.m_port = ntohs(faddr.sin_port);
      }
    }
    return true;
  }
}
