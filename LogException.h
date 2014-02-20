/**************************************************************
* Copyright (c) 2010-2014, Dynamic Network Services, Inc.
* Author - Jake Montgomery (jmontgomery@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#pragma once
#include<string>
#include<stdio.h>

/**
 * Logging exception class.
 * Only used when feature is enabled.
 */
class LogException : public std::exception
{
public:
  LogException();
  LogException(const char *message);
  LogException(const LogException &);
  virtual ~LogException() throw() { };
  virtual const char* what() const throw();
protected:
  std::string m_message;
};
