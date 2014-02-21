/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Base header for various smart pointer type classes.
#include "standard.h"
#include "SmartPointer.h"
#include <unistd.h>

using namespace std;

void CloseFileDescriptor(int val)
{
  ::close(val);
};

void CloseFileHandle(FILE *val)
{
  fclose(val);
}
