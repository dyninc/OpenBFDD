/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
#include "common.h"
#include "SmartPointer.h"
#include "Socket.h"
#include <vector>
#include <errno.h>
#include <fstream>
#include <string.h>
#include <unistd.h>
#include "utils.h"
#include <unistd.h>

using namespace std;

/**
 *
 *
 * @param message
 * @param message_size
 * @param port
 * @param outPrefix [in] - If not null, then every response line will be
 *                  prefixed with this string.
 *
 * @return bool
 */
static bool SendData(const char *message, size_t message_size, const SockAddr &connectAddr, const char *outPrefix = NULL)
{
  size_t totalLength;

  vector<char> buffer(max(MaxReplyLineSize,  MaxCommandSize));
  Socket sendSocket;
  FileHandle fileHandle;
  uint32_t magic;

  if (!sendSocket.OpenTCP(connectAddr.Type()))
  {
    fprintf(stderr, "Error creating %s socket: %s\n",
            Addr::TypeToString(connectAddr.Type()),
            SystemErrorToString(sendSocket.GetLastError()));
    return false;
  }


  if (!sendSocket.Connect(connectAddr))
  {
    fprintf(stderr, "Error connecting to beacon on %s: %s\n",
            connectAddr.ToString(),
            SystemErrorToString(sendSocket.GetLastError()));
    return false;
  }

  // Assemble the message
  magic = htonl(MagicMessageNumber);
  memcpy(&buffer.front(), &magic, sizeof(uint32_t));
  totalLength = message_size + sizeof(uint32_t);
  if (totalLength > MaxCommandSize)
  {
    fputs("Command too long. Not Sent.\n", stderr);
    return false;
  }

  memcpy(&buffer[sizeof(uint32_t)], message, message_size);

  // Send our message.
  if (!sendSocket.Send(&buffer.front(), totalLength))
  {
    fprintf(stderr, "Error sending command to beacon: %s\n", SystemErrorToString(sendSocket.GetLastError()));
    return false;
  }

  // Use stdio for simpler reading of the socket.
  fileHandle = fdopen(sendSocket, "r");
  if (!fileHandle.IsValid())
  {
    perror("Error opening socket file: ");
    return false;
  }

  // Read until done
  while (fgets(&buffer.front(), buffer.size(), fileHandle))
  {
    if (outPrefix)
      fputs(outPrefix, stdout);

    fputs(&buffer.front(), stdout);
  }

  if (ferror(fileHandle))
  {
    perror("\nConnection failed. Partial completion may have occurred: \n");
    return true;
  }

  return true;
}

/**
 * Adds a parameter, if appropriate and possible, to the end of the vector.
 * If the parameter is empty then it is skipped. Adds a null to the end of the
 * buffer.
 *
 * @throw bad_alloc
 *
 * @param buffer
 * @param param
 */
static void AddParamToBuffer(vector<char> &buffer, const char *param)
{
  size_t length = strlen(param) + 1;
  size_t pos;

  if (length == 1)
    return;

  pos = buffer.size();
  buffer.resize(buffer.size() + length);
  memcpy(&buffer[pos], param,  length);
}

static bool doLoadScript(const char *path, const SockAddr &connectAddr)
{
  ifstream file;
  string line;
  int lines = 0;
  vector<char> buffer;
  const char *seps = " \t";

  file.open(path);
  if (!file.is_open())
  {
    fprintf(stderr, "Failed to open file <%s> : %s\n", path, ErrnoToString());
    return false;
  }

  buffer.reserve(MaxCommandSize);
  while (getline(file, line), file.good())
  {
    size_t pos = 0;
    lines++;
    if (line.empty())
      continue;
    if (line[0] == '#')
      continue;

    // Parse the command line. This doe not currently handle quoted parameters. If
    // we ever have a command that takes, for example, a file name, then this will
    // need to be fixed.
    buffer.resize(0);
    pos = line.find_first_not_of(seps, 0);
    while (pos != string::npos)
    {
      size_t sepPos = line.find_first_of(seps, pos);
      LogVerify(sepPos != pos);
      size_t end = (sepPos == string::npos) ? line.length() : sepPos;
      size_t bufpos = buffer.size();
      buffer.resize(bufpos + end - pos);
      memcpy(&buffer[bufpos], &line[pos], end - pos);
      buffer.push_back('\0');
      if (sepPos == string::npos)
        break;
      pos = line.find_first_not_of(seps, end);
    }

    if (buffer.size() != 0)
    {
      fprintf(stdout, " Command <%s>\n", line.c_str());

      // buffer is double null terminated.
      buffer.push_back('\0');
      if (!SendData(&buffer.front(), buffer.size(), connectAddr, "   "))
        return false;
    }
  }

  if (!file.eof())
  {
    fprintf(stderr, "Failed to read from file <%s>. %d lines processed: %s\n", path, lines, ErrnoToString());
    return false;
  }

  file.close();
  return true;
}

static int bfddMain(int argc, char *argv[])
{
  int argIndex;
  SockAddr connectAddr;
  const char *valueString;

  //gLog.LogToFile("/tmp/bfd.log");
  UtilsInit();
  gLog.LogToSyslog("bfdd-control", false);
  gLog.Optional(Log::App, "Startup %x", getpid());

  //Parse command line options
  for (argIndex = 1; argIndex < argc; argIndex++)
  {
    if (0 == strcmp("--altport", argv[argIndex]))
    {
      if (connectAddr.IsValid())
      {
        fprintf(stderr, "Only a single --altport or --control option is allowed.\n");
        exit(1);
      }
      // Backwards compatibility only ... use --control
      connectAddr.FromString("127.0.0.1", ALT_PORTNUM);
    }
    else if (CheckArg("--control", argv[argIndex], &valueString))
    {
      if (!valueString || *valueString == '\0')
      {
        fprintf(stderr, "--control must be followed by an '=' and a ip address with a port.\n");
        exit(1);
      }

      if (!connectAddr.FromString(valueString))
      {
        fprintf(stderr, "--control address <%s> is not an IPv4 or IPv6 address.\n", valueString);
        exit(1);
      }

      if (!connectAddr.HasPort())
      {
        fprintf(stderr, "--control address must have a port specified. The address <%s> does not contain a port.\n", valueString);
        exit(1);
      }
    }
    else if (0 == strncmp("--", argv[argIndex], 2))
    {
      fprintf(stderr, "Unrecognized %s command line option %s.\n", ControlAppName, argv[argIndex]);
      exit(1);
    }
    else
      break;
  }

  if (argIndex >=  argc)
  {
    fprintf(stderr, "No command. Try \"man %s\" for a list of commands.\n", ControlAppName);
    exit(1);
  }

  // "version" is special because we tell first. Than let the beacon handle it.
  if (0 == strcmp(argv[argIndex], "version"))
  {
    fprintf(stdout, "%s v%s\n", ControlAppName, SofwareVesrion);
  }

  if (!connectAddr.IsValid())
    connectAddr.FromString("127.0.0.1", PORTNUM);


  // "load" is special because we send a series of commands..
  if (0 == strcmp(argv[argIndex], "load"))
  {
    argIndex++;

    if (argIndex >=  argc)
    {
      fprintf(stderr, "Must supply a script file after 'load'\n");
      exit(1);
    }

    fprintf(stdout,  "Running script from file <%s>\n", argv[argIndex]);
    if (!doLoadScript(argv[argIndex], connectAddr))
    {
      fprintf(stderr, "Script load failed.\n");
      exit(1);
    }
    fprintf(stdout,  "Completed script from file <%s>\n", argv[argIndex]);
    exit(0);
  }

  // To allow for quotes, we concatenate all the arguments, separating them with
  // "NULL".
  vector<char> buffer;

  buffer.reserve(MaxCommandSize);

  for (; argIndex < argc; argIndex++)
  {
    AddParamToBuffer(buffer, argv[argIndex]);
  }

  if (buffer.size() == 0)
  {
    fprintf(stderr, "No command. Try \"man %s\" for a list of commands.\n", ControlAppName);
    exit(1);
  }

  // argIndex is double null terminated.
  buffer.push_back('\0');

  if (!SendData(&buffer.front(), buffer.size(), connectAddr))
    exit(1);

  exit(0);
}

int main(int argc, char *argv[])
{
  return bfddMain(argc,  argv);
}
