/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jacob Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Base header for various smart pointer type classes.
#include "standard.h"
#include "SmartPointer.h"
#include <ctype.h>

using namespace std;

namespace openbfdd 
{

  void CloseFileDescriptor(int val)
  {
    ::close(val);
  };

  void CloseFileHandle(FILE *val)
  {
    fclose(val);
  }

}





