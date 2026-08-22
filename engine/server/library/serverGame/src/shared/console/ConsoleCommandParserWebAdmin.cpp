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
#include "serverGame/TangibleObject.h"
#include "sharedFoundation/ConstCharCrcLowerString.h"
#include "sharedFoundation/FormattedString.h"
#include "sharedGame/LfgCharacterData.h"
#include "sharedMath/Vector.h"
#include "sharedObject/CachedNetworkId.h"
#include "serverGame/ContainerInterface.h"
#include "serverGame/InstallationObject.h"
#include "sharedObject/Container.h"
#include "sharedObject/NetworkIdManager.h"
#include "sharedObject/SlotIdManager.h"
#include "sharedObject/SlottedContainer.h"
#include "sharedObject/VolumeContainer.h"
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
	{"datapadOf",         1, "<oid>",                                  "The datapad container id for a character."},
	{"giveContainer",     1, "<oid>",                                  "Create a Resource Container in a character's datapad."},
	{"listContainer",     1, "<container oid>",                        "Contents: id|name|template|count."},
	{"moveItem",          2, "<item oid> <container oid>",             "Move an item into a container."},
	{"factoryActivate",   2, "<factory oid> <on|off>",                 "Run or stop a factory as its owner would."},
	{"factoryHoppers",    1, "<factory oid>",                          "input|output hopper ids."},
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

	/** The container a slotted object exposes under a named slot. */
	ServerObject *slotOf(ServerObject &object, const char *slotName)
	{
		SlottedContainer *const slotted = ContainerInterface::getSlottedContainer(object);
		if (!slotted)
			return 0;
		const SlotId slot = SlotIdManager::findSlotId(ConstCharCrcLowerString(slotName));
		Container::ContainerErrorCode code = Container::CEC_Success;
		return dynamic_cast<ServerObject *>(slotted->getObjectInSlot(slot, code).getObject());
	}

	/**
	 * A character's Resource Container, if they have one.
	 *
	 * Found by template rather than by name: a player can rename a container,
	 * and the dashboard still has to find the same object afterwards.
	 */
	ServerObject *findResourceContainer(ServerObject &datapad)
	{
		VolumeContainer *const volume = ContainerInterface::getVolumeContainer(datapad);
		if (!volume)
			return 0;
		for (ContainerIterator i = volume->begin(); i != volume->end(); ++i)
		{
			ServerObject *const item = dynamic_cast<ServerObject *>((*i).getObject());
			if (item && item->getTemplateName() &&
				strstr(item->getTemplateName(), "container/resource_container") != 0)
				return item;
		}
		return 0;
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

			// displayLocationInSearchResults is a setting the player chose, and
			// the in-game search honours it. Withhold the location here rather
			// than in the caller: a second consumer of this command would
			// otherwise have to know to re-apply it, and would not.
			const bool showLocation = character.displayLocationInSearchResults;

			std::string line = i->first.getValueString();
			line += c_fieldSeparator;
			line += Unicode::wideToNarrow(character.characterName);
			line += c_fieldSeparator;
			line += (showLocation ? character.locationPlanet : std::string());
			line += c_fieldSeparator;
			line += (showLocation ? character.locationRegion : std::string());
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
	// The Resource Container, and moving things in and out of it.
	//
	// A hundred-slot store carried in the datapad, so it travels with the
	// character rather than being tied to a house. Everything below works on
	// real game objects through the same container interface the client uses,
	// so what the dashboard creates is an ordinary object the player sees in
	// game.

	if (isAbbrev(argv[0], "datapadOf") || isAbbrev(argv[0], "giveContainer"))
	{
		if (!haveArgs(argv, 1))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		ServerObject *const character = ServerWorld::findObjectByNetworkId(NetworkId(Unicode::wideToNarrow(argv[1])));
		if (!character)
		{
			// An offline character's objects are not loaded, so there is
			// nothing to act on. "Invalid object" would suggest a wrong id when
			// the character is simply not logged in.
			result += Unicode::narrowToWide("that character is not loaded; they must be online\n");
			return true;
		}

		ServerObject *const datapad = slotOf(*character, "datapad");
		if (!datapad)
		{
			result += Unicode::narrowToWide("that character has no datapad\n");
			return true;
		}

		if (isAbbrev(argv[0], "datapadOf"))
		{
			result += Unicode::narrowToWide(datapad->getNetworkId().getValueString() + "\n");
			return true;
		}

		// Idempotent. A second container would split a player's resources
		// between two stores they cannot tell apart.
		ServerObject *const existing = findResourceContainer(*datapad);
		if (existing)
		{
			result += Unicode::narrowToWide(
				FormattedString<192>().sprintf("already has one: %s\n",
					existing->getNetworkId().getValueString().c_str()));
			return true;
		}

		ServerObject *const created = ServerWorld::createNewObject(
			"object/tangible/container/resource_container.iff", *datapad, true);
		if (!created)
		{
			result += Unicode::narrowToWide("could not create the container; is the datapad full?\n");
			return true;
		}

		result += Unicode::narrowToWide(created->getNetworkId().getValueString() + "\n");
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "listContainer"))
	{
		if (!haveArgs(argv, 1))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		ServerObject *const container = ServerWorld::findObjectByNetworkId(NetworkId(Unicode::wideToNarrow(argv[1])));
		if (!container)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		VolumeContainer *const volume = ContainerInterface::getVolumeContainer(*container);
		if (!volume)
		{
			result += Unicode::narrowToWide("that object is not a volume container\n");
			return true;
		}

		// Capacity first, so a caller can show "18 of 100" without counting.
		result += Unicode::narrowToWide(
			FormattedString<64>().sprintf("capacity%c%d%c%d\n",
				c_fieldSeparator, volume->getCurrentVolume(),
				c_fieldSeparator, volume->getTotalVolume()));

		for (ContainerIterator i = volume->begin(); i != volume->end(); ++i)
		{
			ServerObject *const item = dynamic_cast<ServerObject *>((*i).getObject());
			if (!item)
				continue;

			std::string line = item->getNetworkId().getValueString();
			line += c_fieldSeparator;
			line += Unicode::wideToNarrow(item->getEncodedObjectName());
			line += c_fieldSeparator;
			line += (item->getTemplateName() ? item->getTemplateName() : "");
			line += c_fieldSeparator;
			// A stack's count lives on TangibleObject; a non-stacking object
			// reports 0 there, which means "one of it" rather than "none".
			const TangibleObject *const tangible = item->asTangibleObject();
			const int count = tangible ? tangible->getCount() : 0;
			line += FormattedString<16>().sprintf("%d", count > 0 ? count : 1);
			line += '\n';
			result += Unicode::narrowToWide(line);
		}
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "moveItem"))
	{
		if (!haveArgs(argv, 2))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		ServerObject *const item = ServerWorld::findObjectByNetworkId(NetworkId(Unicode::wideToNarrow(argv[1])));
		ServerObject *const destination = ServerWorld::findObjectByNetworkId(NetworkId(Unicode::wideToNarrow(argv[2])));
		if (!item || !destination)
		{
			result += getErrorMessage(argv[0], ERR_INVALID_OBJECT);
			return true;
		}

		// The same transfer the client uses, so capacity, container type and an
		// object's own refusal all apply exactly as they would in game.
		Container::ContainerErrorCode code = Container::CEC_Success;
		const bool moved = ContainerInterface::transferItemToVolumeContainer(
			*destination, *item, 0, code);

		result += Unicode::narrowToWide(
			FormattedString<224>().sprintf("%s %s to %s (code %d)\n",
				moved ? "moved" : "could not move",
				item->getNetworkId().getValueString().c_str(),
				destination->getNetworkId().getValueString().c_str(),
				static_cast<int>(code)));
		return true;
	}

	//-----------------------------------------------------------------
	// Factories.
	//
	// activate() takes the actor id, which is what makes this behave as though
	// the player had used the radial menu -- it is the same call
	// manufacture.java makes. The production loop is the game's own; nothing
	// here reimplements it, so a factory driven from the dashboard runs
	// identically by construction rather than by imitation.

	if (isAbbrev(argv[0], "factoryActivate"))
	{
		if (!haveArgs(argv, 2))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		ServerObject *const object = ServerWorld::findObjectByNetworkId(NetworkId(Unicode::wideToNarrow(argv[1])));
		InstallationObject *const factory = dynamic_cast<InstallationObject *>(object);
		if (!factory)
		{
			result += Unicode::narrowToWide("no installation with that id is loaded\n");
			return true;
		}

		bool wanted = false;
		if (!parseBoolean(argv[2], wanted))
		{
			result += getErrorMessage(argv[0], ERR_INVALID_ARGUMENTS);
			return true;
		}

		if (wanted)
			factory->activate(factory->getOwnerId());
		else
			factory->deactivate();

		// Report what it IS, not what was asked. Activation can be refused --
		// no power, no maintenance, nothing to make -- and echoing the request
		// would report a factory as running when it is not.
		result += Unicode::narrowToWide(
			FormattedString<128>().sprintf("%s%c%s\n",
				factory->getNetworkId().getValueString().c_str(),
				c_fieldSeparator,
				factory->isActive() ? "active" : "inactive"));
		return true;
	}

	//-----------------------------------------------------------------

	if (isAbbrev(argv[0], "factoryHoppers"))
	{
		if (!haveArgs(argv, 1))
		{
			result += getErrorMessage(argv[0], ERR_NOT_ENOUGH_ARGUMENTS);
			return true;
		}

		ServerObject *const factory = ServerWorld::findObjectByNetworkId(NetworkId(Unicode::wideToNarrow(argv[1])));
		if (!factory)
		{
			result += Unicode::narrowToWide("no installation with that id is loaded\n");
			return true;
		}

		ServerObject *const input = slotOf(*factory, "ingredient_hopper");
		ServerObject *const output = slotOf(*factory, "output_hopper");
		result += Unicode::narrowToWide(
			FormattedString<192>().sprintf("%s%c%s\n",
				input ? input->getNetworkId().getValueString().c_str() : "",
				c_fieldSeparator,
				output ? output->getNetworkId().getValueString().c_str() : ""));
		return true;
	}

	//-----------------------------------------------------------------

	result += getErrorMessage(argv[0], ERR_NO_HANDLER);
	return true;
}

// ======================================================================
