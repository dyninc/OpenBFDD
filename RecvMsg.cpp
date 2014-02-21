/**************************************************************
* Copyright (c) 2011, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "RecvMsg.h"
#include "Socket.h"
#include <errno.h>

using namespace std;


RecvMsg::RecvMsg() :
   m_controlBufferSize(0),
   m_dataBufferSize(0),
   m_error(0)
{
  clear();
}

RecvMsg::RecvMsg(size_t bufferSize, size_t controlSize) :
   m_controlBufferSize(0),
   m_dataBufferSize(0),
   m_error(0)
{
  clear();
  AllocBuffers(bufferSize, controlSize);
}

/**
 * Clears everything except the buffers.
 *
 */
void RecvMsg::clear()
{
  m_dataBufferValidSize = 0;
  m_sourceAddress.clear();
  m_destAddress.clear();
  m_ttlOrHops = -1;
  m_error = 0;
}

void RecvMsg::AllocBuffers(size_t bufferSize, size_t controlSize)
{
  m_controlBuffer = new uint8_t[controlSize];
  m_controlBufferSize = controlSize;

  m_dataBuffer = new uint8_t[bufferSize];
  m_dataBufferSize = bufferSize;
  m_dataBufferValidSize = 0;
}

bool RecvMsg::DoRecvMsg(const Socket &socket)
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
  message.msg_name = &msgaddr;
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

bool RecvMsg::DoRecv(const Socket &socket, int flags)
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



uint8_t RecvMsg::GetTTLorHops(bool *success)
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
