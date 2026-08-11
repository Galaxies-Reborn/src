//========================================================================
//
// SwgCreatureObject.cpp
//
// copyright 2001 Sony Online Entertainment
//
//========================================================================

#include "FirstSwgGameServer.h"
#include "SwgGameServer/SwgCreatureObject.h"
#include "serverGame/PlayerCreatureController.h"
#include "serverGame/PlayerObject.h"
#include "serverGame/ServerCreatureObjectTemplate.h"
#include "serverNetworkMessages/MessageToPayload.h"
#include "sharedSkillSystem/SkillObject.h"
#include "SwgGameServer/JediManagerObject.h"
#include "SwgGameServer/SwgPlayerCreatureController.h"
#include "SwgGameServer/SwgPlayerObject.h"
#include "SwgGameServer/SwgServerUniverse.h"

#include <algorithm>
#include <climits>


namespace SwgCreatureObjectNamespace
{
	char const * const cms_jediTitleSkill = "force_title_jedi_rank_02";
	char const * const cms_jediDisciplinePrefix = "force_discipline";
	char const * const cms_forceRankObjvar = "force_rank.rank";
	char const * const cms_smugglerBountyObjvar = "smuggler.bounty";
	char const * const cms_smugglerScriptData = "smuggler";
	char const * const cms_smugglerBountyScriptData = "smugglerBountyValue";

	bool startsWith(std::string const & value, char const * const prefix)
	{
		return value.find(prefix) == 0;
	}

	bool affectsPreCuJediRegistry(std::string const & skillName)
	{
		return skillName == cms_jediTitleSkill ||
			startsWith(skillName, cms_jediDisciplinePrefix);
	}

	JediState getRegistryJediState(SwgCreatureObject const & creature)
	{
		PlayerObject const * const player = PlayerCreatureController::getPlayerObject(&creature);
		if (player != nullptr)
		{
			SwgPlayerObject const * const swgPlayer = safe_cast<SwgPlayerObject const *>(player);
			if (swgPlayer->getJediState() == JS_forceRankedLight)
				return JS_forceRankedLight;
			if (swgPlayer->getJediState() == JS_forceRankedDark)
				return JS_forceRankedDark;
		}
		return JS_jedi;
	}
}

using namespace SwgCreatureObjectNamespace;


//----------------------------------------------------------------------

/**
 * Class constructor.
 */
SwgCreatureObject::SwgCreatureObject(const ServerCreatureObjectTemplate* newTemplate) :
	CreatureObject(newTemplate)
{
}	// SwgCreatureObject::SwgCreatureObject

//----------------------------------------------------------------------

/**
 * Class destructor.
 */
SwgCreatureObject::~SwgCreatureObject()
{
	//-- This must be the first line in the destructor to invalidate any watchers watching this object
	nullWatchers();

}	// SwgCreatureObject::~SwgCreatureObject

//----------------------------------------------------------------------

/**
 * Creates a default controller for this object.
 *
 * @return the object's controller
 */
Controller* SwgCreatureObject::createDefaultController ()
{
	const ServerCreatureObjectTemplate * myTemplate = safe_cast<
		const ServerCreatureObjectTemplate *>(getObjectTemplate());

	Controller * controller = 0;
	if (myTemplate->getCanCreateAvatar())
	{
		controller = new SwgPlayerCreatureController(this);
	}
	else
	{
		controller = CreatureObject::createDefaultController();
	}

	setController(controller);
	return controller;
}	// CreatureObject::createDefaultController

//----------------------------------------------------------------------

/**
 * Called to alter the creature.
 */
float SwgCreatureObject::alter(float time)
{
	if (isAuthoritative() && isPlayerControlled() && (getBountyValue() > 0))
	{
		PlayerObject * const player = PlayerCreatureController::getPlayerObject(this);
		if (player)
		{
			SwgPlayerObject * swgPlayer = safe_cast<SwgPlayerObject *>(player);			
			swgPlayer->updateJediLocationTime(time);
		}
	}
	return CreatureObject::alter(time);
}	// SwgCreatureObject::alter

//----------------------------------------------------------------------

/**
 * Called when the creature is being removed from the world.
 */
void SwgCreatureObject::onRemovingFromWorld()
{
	// if we are a Jedi, flag ourself as offline
	if (isAuthoritative() && isPlayerControlled())
	{
		JediManagerObject * const jediManager = static_cast<SwgServerUniverse &>(ServerUniverse::getInstance()).getJediManager();
		if (jediManager != nullptr && jediManager->isJediRegistered(getNetworkId()))
		{
			jediManager->setJediOffline(getNetworkId(), getPosition_w(), getSceneId());
		}
	}

	CreatureObject::onRemovingFromWorld();
}	// SwgCreatureObject::onRemovingFromWorld

//----------------------------------------------------------------------

/**
 * Called when the creature is being deleted from the game.
 */
void SwgCreatureObject::onPermanentlyDestroyed()
{
	// if we are a Jedi, remove ourself from the Jedi manager
	if (isAuthoritative() && isPlayerControlled())
	{
		JediManagerObject * const jediManager = static_cast<SwgServerUniverse &>(ServerUniverse::getInstance()).getJediManager();
		if (jediManager != nullptr && jediManager->isJediRegistered(getNetworkId()))
		{
			jediManager->removeJedi(getNetworkId());
		}
	}

	CreatureObject::onPermanentlyDestroyed();
}	// SwgCreatureObject::onPermanentlyDestroyed

//----------------------------------------------------------------------

/**
 * Handles messages to this object.
 *
 * @param message		the message
 */
void SwgCreatureObject::handleCMessageTo (const MessageToPayload &message)
{
	CreatureObject::handleCMessageTo (message);
}	// SwgCreatureObject::handleCMessageTo

//----------------------------------------------------------------------

/**
* Checks if a player is a Jedi or not.
*/
bool SwgCreatureObject::isJedi(void) const
{
	return isPlayerControlled() && hasPreCuJediTitle();
}	// SwgCreatureObject::isJedi

//-----------------------------------------------------------------------

bool SwgCreatureObject::hasPreCuJediTitle(void) const
{
	SkillList const & skills = getSkillList();
	for (SkillList::const_iterator i = skills.begin(); i != skills.end(); ++i)
	{
		SkillObject const * const skill = *i;
		if (skill != nullptr && skill->getSkillName() == cms_jediTitleSkill)
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------

/**
* Returns the number of skill points that have been spent on Jedi skills.
*/
const int SwgCreatureObject::getSpentJediSkillPoints() const
{
	int result = 0;

	const SkillList & skills = getSkillList();
	for (SkillList::const_iterator i = skills.begin(); i != skills.end(); ++i)
	{
		if (*i) 
		{
			// Publish 14 counts only the force_discipline trees.
			if (startsWith((*i)->getSkillName(), cms_jediDisciplinePrefix))
				result += (*i)->getSkillPointCost();
		}
		else
		{
			WARNING(true, ("Creature %s had a nullptr in their skill list", getNetworkId().getValueString().c_str()));
		}
	}
	return result;
}	// SwgCreatureObject::getSpentJediSkillPoints

//----------------------------------------------------------------------

int SwgCreatureObject::getPreCuForceRank() const
{
	int rank = 0;
	if (!getObjVars().getItem(cms_forceRankObjvar, rank) || rank < 0 || rank > 11)
		return 0;
	return rank;
}

//----------------------------------------------------------------------

/**
 * Tests if this creature has a bounty on another creature.
 *
 * @param target		the creature to test if we have a bounty on
 *
 * @return true if we have a bounty on the target, false if not
 */
bool SwgCreatureObject::hasBounty(const CreatureObject & target) const
{
	const SwgCreatureObject * swgTarget = dynamic_cast<const SwgCreatureObject *>(
		&target);
	if (swgTarget == nullptr)
		return false;

	JediManagerObject * jediManager = static_cast<SwgServerUniverse &>(
		ServerUniverse::getInstance()).getJediManager();
	NOT_NULL(jediManager);

	return jediManager->hasBountyOnJedi(target.getNetworkId(), getNetworkId());
}	// SwgCreatureObject::hasBounty

// ----------------------------------------------------------------------

/**
 * Tests if this creature has a bounty on any other creature.
 *
 * @return true if we have a bounty, false if not
 */
bool SwgCreatureObject::hasBounty() const
{
	JediManagerObject * jediManager = static_cast<SwgServerUniverse &>(
		ServerUniverse::getInstance()).getJediManager();
	NOT_NULL(jediManager);

	return jediManager->hasBountyOnJedi(getNetworkId());
}

// ----------------------------------------------------------------------

std::vector<NetworkId> const & SwgCreatureObject::getJediBountiesOnMe() const
{
	JediManagerObject * jediManager = static_cast<SwgServerUniverse &>(
		ServerUniverse::getInstance()).getJediManager();
	NOT_NULL(jediManager);

	return jediManager->getJediBounties(getNetworkId());
}

// ----------------------------------------------------------------------

int SwgCreatureObject::getBountyValue() const
{
	if (hasPreCuJediTitle())
	{
		int const forceRank = getPreCuForceRank();
		long long bountyValue = static_cast<long long>(getSpentJediSkillPoints()) * 1000LL +
			static_cast<long long>(forceRank) * 100000LL;
		bountyValue = std::max(25000LL, bountyValue);
		if (forceRank > 0)
			bountyValue = std::max(50000LL, bountyValue);
		return static_cast<int>(std::min(static_cast<long long>(INT_MAX), bountyValue));
	}

	int bountyValue = 0;
	if (getObjVars().getItem(cms_smugglerBountyObjvar, bountyValue) && bountyValue > 0)
		return bountyValue;

	return 0;
}

//------------------------------------------------------------------------------------------

void SwgCreatureObject::synchronizeJediBountyRegistry()
{
	if (!isAuthoritative() || !isPlayerControlled())
		return;

	SwgPlayerObject * const player = safe_cast<SwgPlayerObject *>(PlayerCreatureController::getPlayerObject(this));
	if (player == nullptr)
		return;

	JediManagerObject * const jediManager = static_cast<SwgServerUniverse &>(
		ServerUniverse::getInstance()).getJediManager();
	if (jediManager == nullptr)
		return;

	bool const titleJedi = hasPreCuJediTitle();
	if (titleJedi)
	{
		JediState const registryState = getRegistryJediState(*this);
		if (player->getJediState() != registryState)
		{
			player->setJediState(registryState);
			return;
		}
	}
	else if (player->isJedi())
	{
		// The exact title skill is the Publish 14 admission authority.  Retain
		// force sensitivity without leaving a stale Jedi registry state.
		player->setJediState(JS_forceSensitive);
		return;
	}

	int smugglerBounty = 0;
	bool const smuggler = getObjVars().getItem(cms_smugglerBountyObjvar, smugglerBounty) &&
		smugglerBounty > 0;
	if (!titleJedi && !smuggler)
	{
		if (jediManager->isJediRegistered(getNetworkId()))
			jediManager->removeJedi(getNetworkId());
		return;
	}

	int const bountyValue = titleJedi ? getBountyValue() : smugglerBounty;
	int const visibility = titleJedi ? player->getJediVisibility() : 0;
	JediState const registryState = titleJedi ? getRegistryJediState(*this) : JS_none;
	jediManager->addJedi(getNetworkId(), getObjectName(), getPosition_w(), getSceneId(),
		visibility, bountyValue, 0, 0, registryState, getSpentJediSkillPoints(), getPvpFaction());

	if (smuggler)
	{
		jediManager->updateJediScriptData(getNetworkId(), cms_smugglerScriptData, 1);
		jediManager->updateJediScriptData(getNetworkId(), cms_smugglerBountyScriptData, smugglerBounty);
	}
	else
	{
		jediManager->removeJediScriptData(getNetworkId(), cms_smugglerScriptData);
		jediManager->removeJediScriptData(getNetworkId(), cms_smugglerBountyScriptData);
	}

	if (!isInWorld())
		jediManager->setJediOffline(getNetworkId(), getPosition_w(), getSceneId());
}

//------------------------------------------------------------------------------------------

const bool SwgCreatureObject::grantSkill(const SkillObject & newSkill)
{
	bool const result = CreatureObject::grantSkill(newSkill);
	if (result && isAuthoritative() && affectsPreCuJediRegistry(newSkill.getSkillName()))
		synchronizeJediBountyRegistry();
	return result;
}	// SwgCreatureObject::grantSkill

//-----------------------------------------------------------------------

void SwgCreatureObject::revokeSkill(const SkillObject & oldSkill, bool silent)
{
	CreatureObject::revokeSkill(oldSkill, silent);
	if (isAuthoritative() && !hasSkill(oldSkill) && affectsPreCuJediRegistry(oldSkill.getSkillName()))
		synchronizeJediBountyRegistry();
}	// SwgCreatureObject::revokeSkill

//------------------------------------------------------------------------------------------

void SwgCreatureObject::endBaselines()
{
	CreatureObject::endBaselines();
	synchronizeJediBountyRegistry();
}

//------------------------------------------------------------------------------------------

void SwgCreatureObject::onAddedToWorld()
{
	CreatureObject::onAddedToWorld();
	synchronizeJediBountyRegistry();
}

//------------------------------------------------------------------------------------------

void SwgCreatureObject::levelChanged() const
{
	CreatureObject::levelChanged();

	// Combat level is retired in PRE-CU; keep registry payloads level-neutral.
	if (isAuthoritative() && isPlayerControlled() && (getBountyValue() > 0))
	{
		JediManagerObject * const jediManager = static_cast<SwgServerUniverse &>(
			ServerUniverse::getInstance()).getJediManager();
		if (jediManager != nullptr)
			jediManager->updateJedi(getNetworkId(), -1, -1, 0, -1);
	}
}

//------------------------------------------------------------------------------------------

void SwgCreatureObject::setPvpFaction(Pvp::FactionId factionId)
{
	Pvp::FactionId oldId = getPvpFaction();
	CreatureObject::setPvpFaction(factionId);
	Pvp::FactionId newId = getPvpFaction();
	if ((oldId != newId) && isAuthoritative() && isPlayerControlled())
		synchronizeJediBountyRegistry();
}
