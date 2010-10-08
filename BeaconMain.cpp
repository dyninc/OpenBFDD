/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "Beacon.h"
#include "utils.h"
#include <string.h>

int main (int argc, char *argv[])
{
  int ret;
  openbfdd::Beacon app;
  bool tee = false;
  bool doFork = true;
  int argIndex;


#ifdef BFD_DEBUG
  tee = true;
#endif       

  //Parse command line options. 
  //Be careful what we do here, since we are pre-fork, and we have not initialized 
  //utils, logging, etc. 
  for (argIndex = 1; argIndex < argc; argIndex++)
  {
    if (0 == strcmp("--notee", argv[argIndex]))
    {
      tee=false;
    }
    else if (0 == strcmp("--tee", argv[argIndex]))
    {
      tee=true;
    }
    else if (0 == strcmp("--nofork", argv[argIndex]))
    {
      doFork=false;
    }
    else if (0 == strcmp("--version", argv[argIndex]))
    {
      fprintf(stdout, "%s version %s\n", openbfdd::BeaconAppName, openbfdd::SofwareVesrion);
      exit(0);
    }
    else 
    {
      fprintf(stderr, "Unrecognized %s command line option %s.\n", openbfdd::BeaconAppName, argv[argIndex]);
      exit(1);
    }
  }

  if (doFork)
  {
    tee = false;
    if(0 != daemon(1,0))
    {
      fprintf(stderr, "Failed to daemonize. Exiting.\n");
      exit(1);
    }
  }

  srand(time(NULL));

  if(!openbfdd::UtilsInit() || !openbfdd::UtilsInitThread())
  {
    fprintf(stderr, "Unable to init thread local storage. Exiting.\n");
    exit(1);
  }

  // Setup logging first
  //  openbfdd::gLog.SetLogLevel(openbfdd::Log::Detail);
  openbfdd::gLog.LogToSyslog("bfdd-beacon", tee);
  openbfdd::gLog.Message(openbfdd::Log::App,"Started %d", getpid());

  ret = app.Run();

  openbfdd::gLog.Message(openbfdd::Log::App,"Shutdown %d", getpid());
}

namespace openbfdd
{
  // Global logger;
  Log gLog;
}

