// ======================================================================
//
// ServerCommandPermissionManager.cpp
// copyright (c) 2003 Sony Online Entertainment
//
// =====================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/ServerCommandPermissionManager.h"

#include "serverGame/Client.h"
#include "serverGame/PlayerObject.h"
#include "serverGame/ServerObject.h"
#include "serverGame/ServerWorld.h"
#include "sharedCommandParser/CommandParser.h"
#include "sharedLog/Log.h"
#include "sharedUtility/DataTable.h"
#include "sharedUtility/DataTableManager.h"

#include "UnicodeUtils.h"

ServerCommandPermissionManager::ServerCommandPermissionManager() :
		CommandPermissionManager(),
		m_permissionTable(0)
{
	m_permissionTable = DataTableManager::getTable("datatables/admin/command_permissions.iff", true);
	DEBUG_FATAL(!m_permissionTable, ("Could not open command permissions table"));
	CommandParser::setPermissionManager(this);
}

//------------------------------------------------------------------------------------------

ServerCommandPermissionManager::~ServerCommandPermissionManager()
{
	DataTableManager::close("command_permissions.iff");
	CommandParser::setPermissionManager(0);
}


//------------------------------------------------------------------------------------------

bool ServerCommandPermissionManager::isCommandAllowed (const NetworkId & userId, const Unicode::String & commandPath) const
{
	// commands sent from the ServerConsole program don't have a client associated from them
	// although it is possible to  resolve to a ServerObject
	std::string cmd = Unicode::wideToNarrow(commandPath);

	// Commands the external web dashboard uses. They are held to exactly the
	// same rule as "game" below, and for the same reason: a ServerConsole
	// command has no Client, so the presence of one means a logged-in player is
	// driving the admin console rather than an operator, and that is refused
	// however the command is spelled.
	//
	// Matched on the "webadmin." prefix rather than by listing every
	// subcommand, so adding one cannot silently arrive ungated -- and matched
	// on a prefix that includes the dot, so it cannot be widened by a parser
	// that merely starts with those letters.
	if( cmd.rfind("webadmin.", 0) == 0 )
	{
		ServerObject * const webAdminUser = ServerWorld::findObjectByNetworkId(userId);
		if( !webAdminUser || !webAdminUser->getClient() )
		{
			LOG("ServerCommandPermissionManager", ("Allowing web dashboard command '%s'.", cmd.c_str()) );
			return true;
		}

		LOG("ServerCommandPermissionManager", ("Disallowing web dashboard command '%s' because it has a Client associated with it.", cmd.c_str()));
		return false;
	}

	if( cmd == "game" )
	{
		ServerObject * tmpu = ServerWorld::findObjectByNetworkId(userId);
		// a ServerConsole command sometimes won't have a ServerObject (first time a cluster receives a command)
		// a ServerConsole command will never have a Client associated with it
		if( !tmpu || !tmpu->getClient())
		{
			LOG("ServerCommandPermissionManager", ("Allowing permission to execute ServerConsole command.") );
			return true;
		}
		else
		{
			LOG("ServerCommandPermissionManager", ("Disallowing permission to execute ServerConsole command because it has a Client associated with it."));
			return false;
		}
	}

	ServerObject * user = ServerWorld::findObjectByNetworkId(userId);
	if (!user)
		return false;

	Client* client = user->getClient();
	if (!client)
		return false;

	int clientLevel = client->getGodLevel();
	std::string command = Unicode::wideToNarrow(commandPath);
	int row = m_permissionTable->searchColumnString( 0, command);
	int commandLevel = 5;
	
	if (row != -1)
		commandLevel = m_permissionTable->getIntValue(1, row);

	bool retval =  (commandLevel <= clientLevel);
	if (!retval)
	{
		LOG("CustomerService",("Avatar:%s denied command %s because the command level is %d and they are %d", PlayerObject::getAccountDescription(userId).c_str(), command.c_str(), commandLevel, clientLevel));
	}
	return retval;
}


//------------------------------------------------------------------------------------------
