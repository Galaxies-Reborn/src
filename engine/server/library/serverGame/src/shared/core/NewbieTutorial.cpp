// ======================================================================
//
// NewbieTutorial.cpp
//
// Copyright 2002 Sony Online Entertainment
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/NewbieTutorial.h"

#include "serverGame/ContainerInterface.h"
#include "serverGame/ServerObject.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/ServerWorld.h"
#include "sharedFoundation/NetworkId.h"
#include "sharedGame/SharedBuildoutAreaManager.h"
#include "sharedFile/FileManifest.h"
#include "sharedObject/CachedNetworkId.h"
#include "sharedFile/TreeFile.h"
#include "sharedLog/Log.h"
#include "sharedObject/Container.h"
#include "sharedObject/SlottedContainer.h"
#include "sharedObject/VolumeContainer.h"
#include "sharedObject/CellProperty.h"
#include "sharedObject/PortalProperty.h"
#include "sharedRandom/Random.h"
#include "sharedTerrain/TerrainObject.h"

// ======================================================================

namespace NewbieTutorialNamespace
{
	const std::string        s_sceneId("tutorial");
	const std::string        s_tutorialTemplate("object/building/general/newbie_hall.iff");
	const std::string        s_skippedTutorialTemplate("object/building/general/newbie_hall_skipped.iff");

	const Vector             s_startCoords(0.0f, 0.0f, -3.0f);
	const std::string        s_startCellName("r1");
	const Vector             s_skippedTutorialLocation(0.0f, 0.0f, 400.0f);
	const Vector             s_skippedTutorialStartCoords(27.5f, -4.2f, -159.2f);
	const std::string        s_skippedTutorialStartCellName("r1");
	CachedNetworkId          s_skippedTutorial;

	const float              s_tutorialMapWidth(16384.0f);
	const float              s_tutorialSpacing(512.0f);
	const int                s_sqrtMaxTutorials(static_cast<int>(s_tutorialMapWidth/s_tutorialSpacing));

	const std::string        s_tutorialObjVar("newbie.startTutorial");
	const std::string        s_skippedTutorialObjVar("newbie.startSkippedTutorial");

	const std::string        s_freeTrialPlanets [] = {"tutorial", "space_npe_falcon", "space_ord_mantell"};
	const int                s_numFreeTrialPlanets = sizeof (s_freeTrialPlanets) / sizeof (s_freeTrialPlanets[0]);

	const std::string        s_freeTrialBuildoutPlanet = "dungeon1";

	const std::string        s_freeTrialBuildoutAreas [] = {"npe_shared_station", "npe_dungeon"};
	const int                s_numFreeTrialBuildoutAreas = sizeof (s_freeTrialBuildoutAreas) / sizeof (s_freeTrialBuildoutAreas[0]);
}

using namespace NewbieTutorialNamespace;

std::string const &NewbieTutorial::getSceneId()
{
	return s_sceneId;
}

// ----------------------------------------------------------------------

std::string const &NewbieTutorial::getTutorialTemplateName()
{
	return s_tutorialTemplate;
}

// ----------------------------------------------------------------------

std::string const &NewbieTutorial::getSkippedTutorialTemplateName()
{
	return s_skippedTutorialTemplate;
}

// ----------------------------------------------------------------------

Vector NewbieTutorial::getTutorialLocation()
{
	// Pick a random spot, keep them 512m apart, and don't let them get too close to either axis because we are
	// now using them as server boundaries for multi-server
	float x, z;
	do
	{
		x = s_tutorialSpacing*Random::random(s_sqrtMaxTutorials-1) - s_tutorialMapWidth/2.0f;
		z = s_tutorialSpacing*Random::random(s_sqrtMaxTutorials-1) - s_tutorialMapWidth/2.0f;
	} while (std::abs(x) < 300.0f || std::abs(z) < 300.0f || (x == s_skippedTutorialLocation.x && z == s_skippedTutorialLocation.z));
	return Vector(x, 0.0f, z);
}

// ----------------------------------------------------------------------

Vector const &NewbieTutorial::getSkippedTutorialLocation()
{
	return s_skippedTutorialLocation;
}

// ----------------------------------------------------------------------

ServerObject *NewbieTutorial::createTutorial(Vector const &location)
{
	FATAL(ServerWorld::getSceneId() != s_sceneId, ("Tried to create a character on a non-tutorial server."));

	Transform tr;
	tr.setPosition_p(location);
	ServerObject *tutorial = ServerWorld::createNewObject(
		s_tutorialTemplate,
		tr,
		0,
		false);
	if (tutorial)
		tutorial->addToWorld();
	return tutorial;
}

// ----------------------------------------------------------------------

ServerObject *NewbieTutorial::getOrCreateSkippedTutorial()
{
	FATAL(ServerWorld::getSceneId() != s_sceneId, ("Tried to create the skipped tutorial hall on a non-tutorial server."));

	ServerObject *skippedTutorial = dynamic_cast<ServerObject *>(s_skippedTutorial.getObject());
	if (skippedTutorial)
		return skippedTutorial;

	Transform tr;
	tr.setPosition_p(s_skippedTutorialLocation);
	skippedTutorial = ServerWorld::createNewObject(
		s_skippedTutorialTemplate,
		tr,
		0,
		false);
	if (skippedTutorial)
	{
		skippedTutorial->addToWorld();
		s_skippedTutorial = CachedNetworkId(*skippedTutorial);
	}

	return skippedTutorial;
}

// ----------------------------------------------------------------------

Vector const &NewbieTutorial::getStartCoords()
{
	return s_startCoords;
}

// ----------------------------------------------------------------------

std::string NewbieTutorial::getStartCellName()
{
	return s_startCellName;
}

// ----------------------------------------------------------------------

Vector const &NewbieTutorial::getSkippedTutorialStartCoords()
{
	return s_skippedTutorialStartCoords;
}

// ----------------------------------------------------------------------

std::string NewbieTutorial::getSkippedTutorialStartCellName()
{
	return s_skippedTutorialStartCellName;
}

// ----------------------------------------------------------------------

void NewbieTutorial::setupCharacterForTutorial(ServerObject* character)
{
	if (character)
	{
		character->removeObjVarItem(s_skippedTutorialObjVar);
		character->setObjVarItem(s_tutorialObjVar, 1);
	}
}

// ----------------------------------------------------------------------

void NewbieTutorial::setupCharacterToSkipTutorial(ServerObject* character)
{
	if (character)
	{
		character->removeObjVarItem(s_tutorialObjVar);
		character->setObjVarItem(s_skippedTutorialObjVar, 1);
	}
}

// ----------------------------------------------------------------------

bool NewbieTutorial::shouldStartTutorial(const ServerObject* character)
{
	if (character)
	{
		if (character->getObjVars().hasItem(s_tutorialObjVar))
			return true;
	}

	return false;
}

// ----------------------------------------------------------------------

bool NewbieTutorial::shouldStartSkippedTutorial(const ServerObject* character)
{
	return character && character->getObjVars().hasItem(s_skippedTutorialObjVar);
}

// ----------------------------------------------------------------------

bool NewbieTutorial::isInTutorial(const ServerObject* character)
{
	return shouldStartTutorial(character) || shouldStartSkippedTutorial(character);
}

// ----------------------------------------------------------------------

bool NewbieTutorial::isInTutorialArea(const ServerObject* character)
{
	if (character)
	{
		std::string scene = ServerWorld::getSceneId();

		if (isFreeTrialScene(scene))
			return true;

		Vector position = character->getPosition_w();

		if (isInFreeTrialBuildoutArea(scene, position))
			return true;
	}

	return false;
}

// ----------------------------------------------------------------------

bool NewbieTutorial::isFreeTrialScene(const std::string &scene)
{
	for (int i = 0; i < s_numFreeTrialPlanets; ++i)
	{
		// if the scene matches the name, return true
		if (scene.compare(s_freeTrialPlanets[i]) == 0)
			return true;
		//otherwise, if the scene name is <scene name>_<num>, we return true (multiple zones)
		else if (scene.find(s_freeTrialPlanets[i] + "_", 0) == 0 && scene.length() == (s_freeTrialPlanets[i].length() + 2))
			return true;
	}
	return false;
}

// ----------------------------------------------------------------------

bool NewbieTutorial::isInFreeTrialBuildoutArea(const std::string &scene, const Vector &location)
{
	if (scene.substr(0, s_freeTrialBuildoutPlanet.length()) == s_freeTrialBuildoutPlanet)
	{
		const BuildoutArea *currentBuildout = SharedBuildoutAreaManager::findBuildoutAreaAtPosition(scene.c_str(), location.x, location.z, false);

		if (currentBuildout)
		{
			std::string areaName = currentBuildout->areaName;
			// if the scene matches a buildout area we know about, return true
			for (int i = 0; i < s_numFreeTrialBuildoutAreas; ++i)
				if (areaName.compare(s_freeTrialBuildoutAreas[i]) == 0)
					return true;
		}
	}

	return false;
}

// ----------------------------------------------------------------------

void NewbieTutorial::stripNonFreeAssetsFromPlayerInTutorial(const CreatureObject* character)
{
	if (!character)
		return;

	std::vector<ServerObject *> objectsToDelete;
	const SlottedContainer * const equipmentContainer = ContainerInterface::getSlottedContainer(*character);

	if (equipmentContainer)
		getNonFreeObjectsForDeletion(equipmentContainer, objectsToDelete, character);
	else
		LOG("npe", ("Scanning player %s to stripNonFreeAssets from because they are in a tutorial, but character had no equipment container\n", character->getNetworkId().getValueString().c_str()));

	const ServerObject * inventoryObject = character->getInventory();
	const VolumeContainer * const inventoryContainer = ContainerInterface::getVolumeContainer(*inventoryObject);

	if (inventoryContainer)
		getNonFreeObjectsForDeletion(inventoryContainer, objectsToDelete, character);
	else
		LOG("npe", ("Scanning player %s to stripNonFreeAssets from because they are in a tutorial, but character had no inventory container\n", character->getNetworkId().getValueString().c_str()));

	for (std::vector<ServerObject *>::iterator i = objectsToDelete.begin(); i != objectsToDelete.end(); ++i)
		if (*i)
			(*i)->permanentlyDestroy(DeleteReasons::Player);
}

// ----------------------------------------------------------------------

void NewbieTutorial::getNonFreeObjectsForDeletion(const Container* const container, std::vector<ServerObject *>& objectsToDelete, const CreatureObject* character)
{
	if (!container)
		return;

	for (ContainerConstIterator i(container->begin()); i != container->end(); ++i)
	{
		const CachedNetworkId & itemId = *i;
		ServerObject * item = safe_cast<ServerObject *>(itemId.getObject());
		if (item != nullptr)
		{
			TangibleObject *itemTangible = item->asTangibleObject();
			if (itemTangible != nullptr)
			{
				const char *templateName = itemTangible->getSharedTemplateName();
				if (!FileManifest::contains(templateName))
				{
					objectsToDelete.push_back(item);
					DEBUG_LOG("NewbieTutorialObjectFilter", ("Removing object %s (%s) from player %s because the template is not in the skufree manifest\n", templateName, item->getNetworkId().getValueString().c_str(), character->getNetworkId().getValueString().c_str()));
					LOG("CustomerService", ("Removing object %s (%s) from player %s because the template is not in the skufree manifest\n", templateName, item->getNetworkId().getValueString().c_str(), character->getNetworkId().getValueString().c_str()));
				}
				else
				{
					// see if the item is a container and go through its contents
					const Container * const itemContainer = ContainerInterface::getContainer(*item);
					if (itemContainer != nullptr)
						getNonFreeObjectsForDeletion(itemContainer, objectsToDelete, character);
				}
			}
		}
	}
}

// ======================================================================

