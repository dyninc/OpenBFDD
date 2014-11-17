/**************************************************************
* Copyright (c) 2010-2014, Dynamic Network Services, Inc.
* Author - Jake Montgomery (jmontgomery@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "standard.h"
#include "log.h"
#include <syslog.h>

// Global logger;
LogImp gLog;
LogImp::LogImp() : Logger()
{
  // Defaults have already been set by Logger.

  //
  // Setup Log Types
  //
  m_types[Log::Critical].name = "critical";
  m_types[Log::Critical].logName = "crit";
  m_types[Log::Critical].syslogPriority = LOG_CRIT;

  m_types[Log::Error].name = "error";
  m_types[Log::Error].logName = "error";
  m_types[Log::Error].syslogPriority = LOG_ERR;

  m_types[Log::Warn].name = "warn";
  m_types[Log::Warn].logName = "warn";
  m_types[Log::Warn].syslogPriority = LOG_WARNING;

  m_types[Log::Debug].name = "debug";
  m_types[Log::Debug].logName = "debug";
  m_types[Log::Debug].syslogPriority = LOG_DEBUG;

  m_types[Log::App].name = "app";
  m_types[Log::App].description = "General application messages";

  m_types[Log::AppDetail].name = "app_detail";
  m_types[Log::AppDetail].description = "Detailed application messages";

  m_types[Log::Session].name = "session";
  m_types[Log::Session].description = "Session creation and state change";

  m_types[Log::SessionDetail].name = "session_detail";
  m_types[Log::SessionDetail].description = "Detailed session creation and state change";
  m_types[Log::SessionDetail].syslogPriority = LOG_DEBUG;

  m_types[Log::Discard].name = "discard";
  m_types[Log::Discard].logName = "discard";
  m_types[Log::Discard].description = "Packet discards and errors";

  m_types[Log::DiscardDetail].name = "discard_detail";
  m_types[Log::DiscardDetail].logName = "discard";
  m_types[Log::DiscardDetail].description = "Contents of (some) discarded packets";

  m_types[Log::Packet].name = "packet";
  m_types[Log::Packet].logName = "packet";
  m_types[Log::Packet].description = "Detailed packet info";

  m_types[Log::PacketContents].name = "packet_contents";
  m_types[Log::PacketContents].logName = "packet";
  m_types[Log::PacketContents].description = "Log every non-discarded packet";
  m_types[Log::PacketContents].syslogPriority = LOG_DEBUG;

  m_types[Log::Command].name = "command";
  m_types[Log::Command].logName = "command";
  m_types[Log::Command].description = "Incoming commands";

  m_types[Log::CommandDetail].name = "command_detail";
  m_types[Log::CommandDetail].description = "Detailed info about command processing";
  m_types[Log::CommandDetail].syslogPriority = LOG_DEBUG;

  m_types[Log::TimerDetail].name = "timer_detail";
  m_types[Log::TimerDetail].description = "Detailed info about timers and scheduler";
  m_types[Log::TimerDetail].syslogPriority = LOG_DEBUG;

  m_types[Log::Temp].name = "temp";
  m_types[Log::Temp].description = "Special temporary developer messages";
  m_types[Log::Temp].syslogPriority = LOG_DEBUG;


  //
  // Setup log Levels
  //

  // None
  m_levelsMap[Log::None].name = "none";
  setLevelTypes(Log::None,  false);

  // All
  m_levelsMap[Log::All].name = "all";
  setLevelTypes(Log::All,  true);

  // Minimal
  //
  m_levelsMap[Log::Minimal].name = "minimal";
  copyLevelTypes(Log::None, Log::Minimal);
  m_levelsMap[Log::Minimal].types[Log::Critical] = true;
  m_levelsMap[Log::Minimal].types[Log::Error] = true;
  m_levelsMap[Log::Minimal].types[Log::Warn] = true;

  // Normal
  m_levelsMap[Log::Normal].name = "normal";
  copyLevelTypes(Log::Minimal, Log::Normal);
  m_levelsMap[Log::Normal].types[Log::App] = true;
  m_levelsMap[Log::Normal].types[Log::Session] = true;
  m_levelsMap[Log::Normal].types[Log::Command] = true;

  // Detailed
  m_levelsMap[Log::Detailed].name = "detailed";
  copyLevelTypes(Log::Normal, Log::Detailed);
  m_levelsMap[Log::Detailed].types[Log::Discard] = true;

  // Dev
  m_levelsMap[Log::Dev].name = "dev";
  copyLevelTypes(Log::Detailed, Log::Dev);
  m_levelsMap[Log::Dev].types[Log::Debug] = true;
  m_levelsMap[Log::Dev].types[Log::Packet] = true;
  m_levelsMap[Log::Dev].types[Log::PacketContents] = true;
  m_levelsMap[Log::Dev].types[Log::AppDetail] = true;
  m_levelsMap[Log::Dev].types[Log::SessionDetail] = true;
#ifdef BFD_DEBUG
  m_levelsMap[Log::Dev].types[Log::Temp] = true;
#endif

  SetLogLevel(Log::Normal);
}
