/**************************************************************
* Copyright (c) 2010-2014, Dynamic Network Services, Inc.
* Author - Jake Montgomery (jmontgomery@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "config.h"
#include "LogException.h"

using namespace std;

LogException::LogException() : exception(),
   m_message("LogException")
{
}

LogException::LogException(const char *message) : exception(),
   m_message(message)
{

}

LogException::LogException(const LogException &src) : exception(),
   m_message(src.m_message)
{

}

const char* LogException::what() const throw()
{
  return m_message.c_str();
}
