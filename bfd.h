/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
/**
   Basic headers for the BFD protocol.

   This is based on draft-ietf-bfd-base-10.txt (Jan 5th 2010)
 */
#pragma once

// bfd constants are in a special namespace for clarity
namespace bfd
{
const uint16_t BasePacketSize = 24; // without the auth data.
const uint16_t MaxAuthDataSize = 26; // Keyed SHA1 is the biggest at 26 bytes?
const uint16_t AuthHeaderSize = 2; // just the "fixed" info.
const uint16_t MaxPacketSize = (BasePacketSize + MaxAuthDataSize + AuthHeaderSize);
const uint16_t ListenPort = 3784;
const uint8_t TTLValue = 255;
const uint16_t MinSourcePort = 49142U;  // Per draft-ietf-bfd-v4v6-1hop-11.txt
const uint16_t MaxSourcePort = 65535U;  // Per draft-ietf-bfd-v4v6-1hop-11.txt
const uint8_t Version = 1;    // There has not yet been any official release
const uint32_t BaseMinTxInterval = 1000000L;  // The base "slow" Tx interval.

// State codes
namespace State
{
enum Value
{
  AdminDown = 0,
  Down      = 1,
  Init      = 2,
  Up        = 3,
};
}

const char* StateName(State::Value state);

// Diagnostic codes
namespace Diag
{
enum Value
{
  None = 0,
  ControlDetectExpired = 1,
  EchoFailed = 2,
  NeighborSessionDown = 3,
  ForwardingReset = 4,
  PathDown = 5,
  ConcatPathDown = 6,
  AdminDown = 7,
  ReverseConcatPathDown = 8,
  MaxDiagnostic = 31
};
}

const char* DiagString(Diag::Value diag);
const char* DiagShortString(Diag::Value diag);

// Authentication types
namespace AuthType
{
enum Value
{
  None = 0,
  Password = 1,
  MD5 = 2,
  MeticulousMD5 = 3,
  SHA1 = 4,
  MeticulousSHA1 = 5,
};
}
} // namespace



/**
 * The actual bfd packet structure.
 */
#pragma pack(push, 1)
struct BfdPacketHeader
{
  uint8_t versAndDiag;  // version and diagnostic packed into 1 byte
  uint8_t flags;
  uint8_t detectMult;
  uint8_t length; // Total packet length
  uint32_t myDisc; // My Discriminator
  uint32_t yourDisc; // Your Discriminator
  uint32_t txDesiredMinInt;
  uint32_t rxRequiredMinInt;
  uint32_t rxRequiredMinEchoInt;

  //manipulate bit fields
  inline uint8_t GetVersion() const { return ((versAndDiag & 0xE0) >> 5);}
  inline void SetVersion(uint8_t ver) { versAndDiag = ((ver & 0x07) << 5) | (versAndDiag & 0x1F);}
  inline bfd::Diag::Value GetDiag() const  { return bfd::Diag::Value(versAndDiag & 0x1F);}
  inline void SetDiag(bfd::Diag::Value diag) { versAndDiag = ((uint8_t)diag & 0x1F) | (versAndDiag & 0xE0);}
  inline bfd::State::Value GetState() const { return bfd::State::Value((flags >> 6) & 0x03);}
  inline void SetState(bfd::State::Value state) { flags = (((uint8_t)state & 0x03) << 6) | (flags & 0x3F);}
  inline bool GetPoll() const { return (flags & 0x20);}
  inline void SetPoll(bool val) { flags = val ? flags | 0x20 : flags & ~0x20;}
  inline bool GetFinal() const { return (flags & 0x10);}
  inline void SetFinal(bool val) { flags = val ? flags | 0x10 : flags & ~0x10;}
  inline bool GetControlPlaneIndependent() const { return (flags & 0x08);}
  inline void SetControlPlaneIndependent(bool val) { flags = val ? flags | 0x08 : flags & ~0x08;}
  inline bool GetAuth() const { return (flags & 0x04);}
  inline void SetAuth(bool val) { flags = val ? flags | 0x04 : flags & ~0x04;}
  inline bool GetDemand() const { return (flags & 0x02);}
  inline void SetDemand(bool val) { flags = val ? flags | 0x02 : flags & ~0x02;}
  inline bool GetMultipoint() const { return (flags & 0x01);}
  inline void SetMultipoint(bool val) { flags = val ? flags | 0x01 : flags & ~0x01;}
};
#pragma pack(pop)

/**
 * Optional Authentication header.
 */
#pragma pack(push, 1)
struct BFDAuthData
{
  uint8_t type;
  uint8_t len;
  uint8_t data[bfd::MaxAuthDataSize];  // enough room for the largest.

  inline bfd::AuthType::Value GetAuthType() const { return bfd::AuthType::Value(type);}
  inline void SetAuthType(bfd::AuthType::Value val) { type = (uint8_t)val;}
};
#pragma pack(pop)


#pragma pack(push, 1)
struct BfdPacket
{
  BfdPacketHeader header;
  BFDAuthData auth;
};
#pragma pack(pop)
