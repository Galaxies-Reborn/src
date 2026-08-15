// ServerConsole.cpp
// Copyright 2000-02, Sony Online Entertainment Inc., all rights reserved. 
// Author: Justin Randall

//-----------------------------------------------------------------------

#include "FirstServerConsole.h"
#include "ConfigServerConsole.h"
#include "sharedFoundation/Os.h"
#include "sharedNetwork/Connection.h"
#include "sharedNetworkMessages/ConsoleChannelMessages.h"
#include "ServerConsole.h"
#include "ServerConsoleConnection.h"
#include <cstdio>
#include <string>

//-----------------------------------------------------------------------

namespace ServerConsoleNamespace
{
	ServerConsoleConnection * s_serverConnection = 0;
	bool                   s_done = false;
}

using namespace ServerConsoleNamespace;

//-----------------------------------------------------------------------

ServerConsole::ServerConsole()
{
}

//-----------------------------------------------------------------------

ServerConsole::~ServerConsole()
{
}

//-----------------------------------------------------------------------

void ServerConsole::done()
{
	s_done = true;
}

//-----------------------------------------------------------------------

void ServerConsole::run()
{
	if(!ConfigServerConsole::getServerAddress())
		return;

	if(!ConfigServerConsole::getServerPort())
		return;

	if(stdin)
	{
		std::string input;
		char inBuf[1024] = {"\0"};
		// fread's element size must be 1, not the buffer size. Asking for one
		// 1024-byte element returns 0 for any short read, and a command is
		// almost always short -- so every command under 1 KB was read into the
		// buffer and then thrown away, and the console answered "Nothing to
		// send to the server" no matter what it was given. Counting bytes also
		// means embedded NULs cannot truncate the command, which appending a
		// char array did.
		size_t bytesRead = 0;
		while ((bytesRead = fread(inBuf, 1, sizeof(inBuf), stdin)) > 0)
		{
			input.append(inBuf, bytesRead);
		}

		if(input.length() > 0)
		{
			// connect to the server
			s_serverConnection = new ServerConsoleConnection(ConfigServerConsole::getServerAddress(), ConfigServerConsole::getServerPort());
			ConGenericMessage msg(input);
			s_serverConnection->send(msg);

			while(! s_done)
			{
				NetworkHandler::update();
				NetworkHandler::dispatch();
				Os::sleep(1);
			}
		}
		else
		{
			fprintf(stderr, "Nothing to send to the server. Aborting");
		}
	}
}

//-----------------------------------------------------------------------
