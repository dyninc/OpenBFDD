/**************************************************************
* Copyright (c) 2010-2014, Dynamic Network Services, Inc.
* Author - Jake Montgomery (jmontgomery@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once

#include "Logger.h"

/**
 * Logging types
 */
struct Log
{
  enum Level
  {
    None = 0,
    Minimal,
    Normal,
    Detailed,
    Dev,  // This will change with the developers whim.
    All,  // be careful .. this is a lot of info.

    LevelCount // used only to signify error
  };

  enum Type
  {
    Critical = 0, // Serious failure.
    Error, // Major failure
    Warn, // Problematic condition
    Debug,
    App, // General important info about app
    AppDetail, // Detailed info about app.
    Session, // Session creation and state change.
    SessionDetail, // Session creation and state change.
    Discard, // Packet discards and errors
    DiscardDetail, // Contents of (some) discarded packets.
    Packet, // Detailed packet info
    PacketContents, // Log every non-discarded packet.
    Command, // Commands
    CommandDetail, // Detailed info about command processing
    TimerDetail, // Detailed info about timers and scheduler.
    Temp,  // Special temporary log messages

    TypeCount // used only to signify error
  };
};
