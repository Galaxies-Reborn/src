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
#include "sharedSkillSystem/SkillManager.h"
#include "sharedSkillSystem/SkillObject.h"
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
	{"grantCredits",      3, "<oid> <amount> <account>",               "Move credits between a character's bank and a named account."},
	{"grantXp",           3, "<oid> <type> <amount>",                  "Grant or remove experience. Negative amounts remove."},
	{"createItem",        2, "<container oid> <template>",             "Create an object inside a container."},
	{"destroyItem",       1, "<oid>",                                  "Permanently destroy an object."},
	{"inventoryOf",       1, "<oid>",                                  "The inventory container id for a character."},
	{"grantSkill",        2, "<oid> <skill>",                          "Grant a skill to a character."},
	{"revokeSkill",       2, "<oid> <skill>",                          "Revoke a skill from a character."},
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

	/**
	 * Check a command has the arguments it is about to read.
	 *
	 * CommandParser::parse() would normally enforce the minimum from the
	 * CmdInfo table, but this parser is invoked through performParsing directly
	 * -- see CentralCommandParserGame, which has to skip parse() to get past
	 * the permission manager. Nothing else stands between a short command and
	 * an out-of-range argv[], and std::vector does not bounds check.
	 */
	bool haveArgs(const CommandParser::StringVector_t &argv, size_t needed)
	{
		return argv.size() > needed;
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
		if (!haveArgs(argv, 5))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

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

		// requestSceneWarp answers whether it accepted the request; it refuses
		// an unknown scene, among other things. Discarding that made every
		// warp report success, including the ones that never happened.
		const bool accepted = GameServer::getInstance().requestSceneWarp(
			CachedNetworkId(*target),
			sceneName,
			position,
			NetworkId::cms_invalid,  // no containing cell: an outdoor position
			position,
			0,
			false);

		if (!accepted)
		{
			result += Unicode::narrowToWide(
				FormattedString<256>().sprintf("failed to warp %s to %s; check the scene name\n",
					targetId.getValueString().c_str(), sceneName.c_str()));
			return true;
		}

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
		if (!haveArgs(argv, 3))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

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
		if (!haveArgs(argv, 2))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

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
		if (!haveArgs(argv, 2))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

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
	// Credits, experience and items.
	//
	// The stock console already does all three, but only for a GM logged into
	// the game: those handlers resolve the *invoking* character and act
	// relative to it, and `money` calls WARNING_STRICT_FATAL when that lookup
	// fails. A command arriving from ServerConsole has no invoking character
	// at all, so they cannot be reused and are restated here against an
	// explicit target.

	if (isAbbrev(argv[0], "grantCredits"))
	{
		if (!haveArgs(argv, 3))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		ServerObject *const target = dynamic_cast<ServerObject *>(NetworkIdManager::getObjectById(NetworkId(Unicode::wideToNarrow(argv[1]))));
		if (!target)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		const int amount = atoi(Unicode::wideToNarrow(argv[2]).c_str());
		if (amount == 0)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_ARGUMENTS);
			return true;
		}

		// Credits are conserved: they are moved to or from a named system
		// account rather than conjured, so the transaction has somewhere to
		// point back to.
		const std::string account = Unicode::wideToNarrow(argv[3]);
		const bool ok = (amount > 0)
			? target->transferBankCreditsFrom(account, amount)
			: target->transferBankCreditsTo(account, -amount);

		result += Unicode::narrowToWide(
			FormattedString<192>().sprintf("%s %d credits %s %s for %s\n",
				ok ? "moved" : "failed to move", abs(amount),
				amount > 0 ? "from" : "to", account.c_str(),
				target->getNetworkId().getValueString().c_str()));
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "grantXp"))
	{
		if (!haveArgs(argv, 3))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		CreatureObject *const creature = dynamic_cast<CreatureObject *>(NetworkIdManager::getObjectById(NetworkId(Unicode::wideToNarrow(argv[1]))));
		if (!creature)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		const std::string experienceType = Unicode::wideToNarrow(argv[2]);
		const int amount = atoi(Unicode::wideToNarrow(argv[3]).c_str());
		if (amount == 0)
		{
			// atoi cannot tell "0" from unparseable, and granting nothing is
			// never what the caller meant by either.
			result += Unicode::narrowToWide("amount must be a non-zero integer\n");
			return true;
		}

		// The return is the amount actually granted, not a running total, and
		// it is 0 when the grant was refused -- a retired NGE progression type,
		// or a creature with no PlayerObject. Printing it as a total made a
		// refusal read as a successful grant.
		const int granted = creature->grantExperiencePoints(experienceType, amount);
		if (granted == 0)
		{
			result += Unicode::narrowToWide(
				FormattedString<224>().sprintf("no experience granted to %s; %s may not be a valid type for this character\n",
					creature->getNetworkId().getValueString().c_str(), experienceType.c_str()));
			return true;
		}

		result += Unicode::narrowToWide(
			FormattedString<224>().sprintf("granted %d %s to %s\n",
				granted, experienceType.c_str(),
				creature->getNetworkId().getValueString().c_str()));
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "createItem"))
	{
		if (!haveArgs(argv, 2))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		ServerObject *const container = dynamic_cast<ServerObject *>(NetworkIdManager::getObjectById(NetworkId(Unicode::wideToNarrow(argv[1]))));
		if (!container)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		const std::string templateName = Unicode::wideToNarrow(argv[2]);
		// Persisted, because an item granted by an administrator that vanishes
		// on the next restart is worse than one that was never granted.
		ServerObject *const created = ServerWorld::createNewObject(templateName, *container, true);
		if (!created)
		{
			result += Unicode::narrowToWide("could not create " + templateName + "; check the template path and the container's capacity\n");
			return true;
		}

		result += Unicode::narrowToWide(
			FormattedString<256>().sprintf("created %s as %s in %s\n",
				templateName.c_str(),
				created->getNetworkId().getValueString().c_str(),
				container->getNetworkId().getValueString().c_str()));
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "destroyItem"))
	{
		if (!haveArgs(argv, 1))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		ServerObject *const object = dynamic_cast<ServerObject *>(NetworkIdManager::getObjectById(NetworkId(Unicode::wideToNarrow(argv[1]))));
		if (!object)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		// Nothing upstream stops an operator pasting a character id here, and
		// permanentlyDestroy would take it. A player is never an "item".
		const CreatureObject *const asCreature = object->asCreatureObject();
		if (asCreature && asCreature->isPlayerControlled())
		{
			result += Unicode::narrowToWide("refusing to destroy a player character\n");
			return true;
		}

		const std::string id = object->getNetworkId().getValueString();
		const bool ok = object->permanentlyDestroy(DeleteReasons::God);
		result += Unicode::narrowToWide(
			FormattedString<128>().sprintf("%s %s\n", ok ? "destroyed" : "failed to destroy", id.c_str()));
		return true;
	}

	//-----------------------------------------------------------------
	// The container id, not the character's: items are created in the
	// inventory, and the caller has only the character.

	if (isAbbrev(argv[0], "inventoryOf"))
	{
		if (!haveArgs(argv, 1))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		CreatureObject *const creature = dynamic_cast<CreatureObject *>(NetworkIdManager::getObjectById(NetworkId(Unicode::wideToNarrow(argv[1]))));
		if (!creature)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		const ServerObject *const inventory = creature->getInventory();
		if (!inventory)
		{
			result += Unicode::narrowToWide("that character has no inventory container\n");
			return true;
		}

		result += Unicode::narrowToWide(inventory->getNetworkId().getValueString() + "\n");
		return true;
	}

	//-----------------------------------------------------------------
	// Skills.
	//
	// Same reason as the rest: `skill grantSkill` resolves the invoking
	// character when no oid is given, and there is no invoking character here.
	// The target is required rather than optional so it can never silently
	// fall back to nobody.

	if (isAbbrev(argv[0], "grantSkill") || isAbbrev(argv[0], "revokeSkill"))
	{
		if (!haveArgs(argv, 2))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		CreatureObject *const creature = dynamic_cast<CreatureObject *>(NetworkIdManager::getObjectById(NetworkId(Unicode::wideToNarrow(argv[1]))));
		if (!creature)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		const std::string skillName = Unicode::wideToNarrow(argv[2]);
		const SkillObject *const skill = SkillManager::getInstance().getSkill(skillName);
		if (!skill)
		{
			// Naming the skill back is the whole diagnostic: the usual cause is
			// a typo or a skill that does not exist in this build's tree.
			result += Unicode::narrowToWide("no such skill: " + skillName + "\n");
			return true;
		}

		const bool granting = isAbbrev(argv[0], "grantSkill");
		if (granting)
			IGNORE_RETURN(creature->grantSkill(*skill));
		else
			creature->revokeSkill(*skill);

		result += Unicode::narrowToWide(
			FormattedString<192>().sprintf("%s %s %s %s\n",
				granting ? "granted" : "revoked", skillName.c_str(),
				granting ? "to" : "from",
				creature->getNetworkId().getValueString().c_str()));
		return true;
	}

	//-----------------------------------------------------------------

	result += getErrorMessage(argv[0], ERR_NO_HANDLER);
	return true;
}

// ======================================================================
