// ======================================================================
//
// ConsoleCommandParserWebAdmin.h
// copyright (c) 2026 PRE-CU Reborn
//
// ======================================================================

#ifndef INCLUDED_ConsoleCommandParserWebAdmin_H
#define INCLUDED_ConsoleCommandParserWebAdmin_H

#include "sharedCommandParser/CommandParser.h"

// ======================================================================

/**
* Commands the external web dashboard needs and the stock console does not
* provide.
*
* Each of these exists because the information or the action is unreachable
* from outside the running cluster:
*
*  - Connected population lives only in memory. Neither the game schema nor
*    the login schema records who is online, so a dashboard reading the
*    database can only report a character total.
*  - Cross-planet movement cannot be expressed by "object move", which sets
*    coordinates within whichever scene already owns the object.
*  - Vendor settings are held in memory by the CommoditiesServer and rewritten
*    on its save cycle, so a direct database update is silently overwritten.
*
* Output is deliberately machine-readable: one record per line, pipe
* separated, so the caller can parse it without guessing at prose.
*/

class ConsoleCommandParserWebAdmin : public CommandParser
{
public:
	                                 ConsoleCommandParserWebAdmin ();
	virtual bool                     performParsing (const NetworkId & userId, const StringVector_t & argv, const String_t & originalCommand, String_t & result, const CommandParser * node);

private:
	                                 ConsoleCommandParserWebAdmin (const ConsoleCommandParserWebAdmin & rhs);
	ConsoleCommandParserWebAdmin &   operator= (const ConsoleCommandParserWebAdmin & rhs);
};

// ======================================================================

#endif	// INCLUDED_ConsoleCommandParserWebAdmin_H
