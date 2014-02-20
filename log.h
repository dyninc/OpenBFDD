/**************************************************************
* Copyright (c) 2010-2014, Dynamic Network Services, Inc.
* Author - Jake Montgomery (jmontgomery@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once

#include "Logger.h"

/**
 * Logging class. Is thread safe.
 * Only create one per process.
 */
class LogImp : public Logger
{
public:
  LogImp();
};

extern LogImp gLog;
