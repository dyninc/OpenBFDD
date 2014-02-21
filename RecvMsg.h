/**************************************************************
* Copyright (c) 2011, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once

#include "SockAddr.h"
#include "SmartPointer.h"

class Socket;

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
  int GetLastError() { return m_error;}

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
  const IpAddr& GetDestAddress() { return m_destAddress;}

  /**
   * The source address.
   *
   * @return SockAddr - IsValid() will return true on failure.
   */
  const SockAddr& GetSrcAddress() { return m_sourceAddress;}

  /**
   * Gets the data from the last DoRecvMsg(), if successful.
   *
   *
   * @return - Data from the last DoRecvMsg(), if successful. NULL if DoRecvMsg
   *         was never called, or it failed.
   */
  uint8_t* GetData() { return m_dataBufferValidSize ? m_dataBuffer.val : NULL;}

  /**
   * Gets the size of the data from the last DoRecvMsg(), if successful.
   *
   *
   * @return - The size of valid data from the last DoRecvMsg(), if
   *         successful. 0 if DoRecvMsg was never called, or it failed.
   */
  size_t GetDataSize() { return m_dataBufferValidSize;}

private:
  void clear();
private:
  Raii<uint8_t>::DeleteArray m_controlBuffer; // Not using vector, because we do not want initialization.
  size_t m_controlBufferSize;
  Raii<uint8_t>::DeleteArray m_dataBuffer; // Not using vector, because we do not want initialization.
  size_t m_dataBufferSize;
  size_t m_dataBufferValidSize;  // Only valid after successful DoRecvMsg
  SockAddr m_sourceAddress;
  IpAddr m_destAddress;
  int16_t m_ttlOrHops; // -1 for invalid
  int m_error;
};
