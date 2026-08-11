// ======================================================================
//
// CommandCppFuncs.h
//
// Copyright 2002 Sony Online Entertainment
//
// ======================================================================

#ifndef INCLUDED_CommandCppFuncs_H
#define INCLUDED_CommandCppFuncs_H

#include <vector>

class Command;
class NetworkId;

class CommandCppFuncs // static class
{
public:
	static void install();
	static void remove();
	static bool getPrecuCtsStatAllocation(NetworkId const & actor, std::vector<int> & allocation);
	static bool applyPrecuCtsStatAllocation(NetworkId const & actor, std::vector<int> const & allocation);
	static bool canCommitStatMigration(NetworkId const & actor);
	static bool commitStatMigration(NetworkId const & actor);

	static void commandFuncTransferMisc(Command const & c, NetworkId const &actor, NetworkId const &target, Unicode::String const & params);
};

#endif // INCLUDED_CommandCppFuncs_H
