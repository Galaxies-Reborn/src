// ======================================================================
//
// ConsoleCommandParserWebAdmin.cpp
// copyright (c) 2026 PRE-CU Reborn
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/ConsoleCommandParserWebAdmin.h"

#include "serverGame/CommoditiesMarket.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/GameServer.h"
#include "serverGame/ServerObject.h"
#include "serverGame/ServerUniverse.h"
#include "serverGame/ServerWorld.h"
#include "sharedFoundation/FormattedString.h"
#include "sharedGame/LfgCharacterData.h"
#include "sharedMath/Vector.h"
#include "sharedObject/CachedNetworkId.h"
#include "sharedObject/NetworkIdManager.h"
#include "UnicodeUtils.h"

#include <map>
#include <string>

// ======================================================================

static const CommandParser::CmdInfo cmds[] =
{
	{"playerCount",       0, "",                                        "Number of characters currently connected to the galaxy."},
	{"whoList",           0, "[limit]",                                 "Connected characters: id|name|planet|region|guild|anonymous."},
	{"warpPlayer",        5, "<oid> <scene> <x> <y> <z>",               "Move a player to another planet."},
	{"vendorSetTax",      3, "<vendor oid> <percent> <bank oid>",       "Set a vendor's sales tax and the account it pays into."},
	{"vendorSetEntrance", 2, "<vendor oid> <credits>",                  "Set a vendor's entrance charge."},
	{"vendorSetSearch",   2, "<vendor oid> <on|off>",                   "Set whether a vendor appears in bazaar searches."},
	{"", 0, "", ""} // this must be last
};

// ======================================================================

namespace ConsoleCommandParserWebAdminNamespace
{
	// A pipe is safe as a field separator here: character names cannot contain
	// one, and planet and region names are engine identifiers.
	const char c_fieldSeparator = '|';

	/**
	 * Resolve a vendor by object id.
	 *
	 * Either the vendor object or its bazaar container is accepted, because
	 * getBazaarContainer() returns a creature's inventory but any other object
	 * itself. A caller reading auction_locations has the container id; a caller
	 * looking at the world has the vendor. Both land on the same container.
	 *
	 * ServerWorld rather than NetworkIdManager deliberately: setEntranceCharge
	 * and updateVendorSearchOption re-resolve through ServerWorld and silently
	 * return when it misses. Looking the object up any other way here would let
	 * this report success for a call that did nothing.
	 */
	ServerObject *findVendor(const Unicode::String &argument)
	{
		const NetworkId vendorId(Unicode::wideToNarrow(argument));
		return ServerWorld::findObjectByNetworkId(vendorId);
	}

	bool parseBoolean(const Unicode::String &argument, bool &value)
	{
		const std::string text = Unicode::toLower(Unicode::wideToNarrow(argument));
		if (text == "on" || text == "true" || text == "1" || text == "yes")
		{
			value = true;
			return true;
		}
		if (text == "off" || text == "false" || text == "0" || text == "no")
		{
			value = false;
			return true;
		}
		return false;
	}
}

using namespace ConsoleCommandParserWebAdminNamespace;

// ======================================================================

ConsoleCommandParserWebAdmin::ConsoleCommandParserWebAdmin (void) :
CommandParser ("webadmin", 0, "...", "Commands for the external web dashboard.", 0)
{
	createDelegateCommands (cmds);
}

//-----------------------------------------------------------------

bool ConsoleCommandParserWebAdmin::performParsing (const NetworkId & userId, const StringVector_t & argv, const String_t & originalCommand, String_t & result, const CommandParser * node)
{
	NOT_NULL (node);
	UNREF (userId);
	UNREF (originalCommand);

	//-----------------------------------------------------------------
	// Population.
	//
	// ServerUniverse holds this map for the whole galaxy, not just this game
	// server, because it is what drives /who. That makes it the only correct
	// source for a concurrency figure.

	if (isAbbrev(argv[0], "playerCount"))
	{
		const std::map<NetworkId, LfgCharacterData> &connected = ServerUniverse::getConnectedCharacterLfgData();
		result += Unicode::narrowToWide(FormattedString<32>().sprintf("%d\n", static_cast<int>(connected.size())));
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "whoList"))
	{
		const std::map<NetworkId, LfgCharacterData> &connected = ServerUniverse::getConnectedCharacterLfgData();

		// An unbounded list on a busy galaxy would be megabytes down a console
		// connection that reads one reply; default to something a dashboard
		// page can use and let the caller ask for more.
		int limit = 200;
		if (argv.size() > 1)
		{
			const int requested = atoi(Unicode::wideToNarrow(argv[1]).c_str());
			if (requested > 0)
				limit = requested;
		}

		int emitted = 0;
		for (std::map<NetworkId, LfgCharacterData>::const_iterator i = connected.begin(); i != connected.end() && emitted < limit; ++i, ++emitted)
		{
			const LfgCharacterData &character = i->second;

			std::string line = i->first.getValueString();
			line += c_fieldSeparator;
			line += Unicode::wideToNarrow(character.characterName);
			line += c_fieldSeparator;
			line += character.locationPlanet;
			line += c_fieldSeparator;
			line += character.locationRegion;
			line += c_fieldSeparator;
			line += character.guildName;
			line += c_fieldSeparator;
			line += (character.anonymous ? "1" : "0");
			line += '\n';

			result += Unicode::narrowToWide(line);
		}

		// State the truncation rather than letting a capped list read as the
		// whole population.
		if (static_cast<int>(connected.size()) > emitted)
			result += Unicode::narrowToWide(FormattedString<64>().sprintf("... %d more\n", static_cast<int>(connected.size()) - emitted));

		return true;
	}

	//-----------------------------------------------------------------
	// Cross-planet movement.
	//
	// "object move" only sets coordinates inside the scene that already owns
	// the object; which PlanetServer that is cannot change that way.
	// requestSceneWarp is the same path the warpPlayer script method takes.

	if (isAbbrev(argv[0], "warpPlayer"))
	{
		const NetworkId targetId(Unicode::wideToNarrow(argv[1]));
		ServerObject *const target = dynamic_cast<ServerObject *>(NetworkIdManager::getObjectById(targetId));
		if (!target)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		const std::string sceneName = Unicode::wideToNarrow(argv[2]);
		if (sceneName.empty())
		{
			result += getErrorMessage(argv[0], ERR_INVALID_ARGUMENTS);
			return true;
		}

		const Vector position(
			static_cast<float>(atof(Unicode::wideToNarrow(argv[3]).c_str())),
			static_cast<float>(atof(Unicode::wideToNarrow(argv[4]).c_str())),
			static_cast<float>(atof(Unicode::wideToNarrow(argv[5]).c_str())));

		GameServer::getInstance().requestSceneWarp(
			CachedNetworkId(*target),
			sceneName,
			position,
			NetworkId::cms_invalid,  // no containing cell: an outdoor position
			position,
			0,
			false);

		result += Unicode::narrowToWide(
			FormattedString<256>().sprintf("warped %s to %s %.2f %.2f %.2f\n",
				targetId.getValueString().c_str(), sceneName.c_str(),
				position.x, position.y, position.z));
		return true;
	}

	//-----------------------------------------------------------------
	// Vendor settings.
	//
	// These live in the CommoditiesServer's memory. Going through
	// CommoditiesMarket sends it the same messages the in-game vendor UI does,
	// so the change survives its next save instead of being overwritten.

	if (isAbbrev(argv[0], "vendorSetTax"))
	{
		ServerObject *const vendor = findVendor(argv[1]);
		if (!vendor)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		// setSalesTax wants the auction container; the other two look it up
		// themselves from the vendor.
		ServerObject *const auctionContainer = vendor->getBazaarContainer();
		if (!auctionContainer)
		{
			result += Unicode::narrowToWide("that object has no bazaar container; it is not a vendor\n");
			return true;
		}

		const int32 salesTax = static_cast<int32>(atoi(Unicode::wideToNarrow(argv[2]).c_str()));
		const NetworkId bankId(Unicode::wideToNarrow(argv[3]));

		CommoditiesMarket::setSalesTax(salesTax, bankId, *auctionContainer);

		result += Unicode::narrowToWide(
			FormattedString<128>().sprintf("sales tax for %s set to %d\n",
				vendor->getNetworkId().getValueString().c_str(), static_cast<int>(salesTax)));
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "vendorSetEntrance"))
	{
		ServerObject *const vendor = findVendor(argv[1]);
		if (!vendor)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		const int entranceCharge = atoi(Unicode::wideToNarrow(argv[2]).c_str());
		CommoditiesMarket::setEntranceCharge(vendor->getNetworkId(), entranceCharge);

		result += Unicode::narrowToWide(
			FormattedString<128>().sprintf("entrance charge for %s set to %d\n",
				vendor->getNetworkId().getValueString().c_str(), entranceCharge));
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "vendorSetSearch"))
	{
		ServerObject *const vendor = findVendor(argv[1]);
		if (!vendor)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		bool enabled = false;
		if (!parseBoolean(argv[2], enabled))
		{
			result += getErrorMessage(argv[0], ERR_INVALID_ARGUMENTS);
			return true;
		}

		CommoditiesMarket::updateVendorSearchOption(vendor->getNetworkId(), enabled);

		result += Unicode::narrowToWide(
			FormattedString<128>().sprintf("search for %s set to %s\n",
				vendor->getNetworkId().getValueString().c_str(), enabled ? "on" : "off"));
		return true;
	}

	//-----------------------------------------------------------------

	result += getErrorMessage(argv[0], ERR_NO_HANDLER);
	return true;
}

// ======================================================================
