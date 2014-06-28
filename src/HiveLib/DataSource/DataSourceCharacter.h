#pragma once

#include "DataSource.h"

class CharDataSource
{
public:
	virtual ~CharDataSource() {}

	virtual Sqf::Value fetchCharacterInitial( string playerId, int serverId, const string& playerName ) = 0;
	virtual Sqf::Value fetchCharacterDetails( int characterId ) = 0;
	typedef map<string, Sqf::Value> FieldsType;
	virtual bool updateCharacter( int characterId, const FieldsType& fields ) = 0;
	virtual bool killCharacter( int characterId, int duration ) = 0;
	virtual bool initCharacter( int characterId, const Sqf::Value& inventory, const Sqf::Value& backpack ) = 0;
	virtual bool recordLogEntry( string playerId, int characterId, int serverId, int action ) = 0;

protected:
	static int SanitiseInv(Sqf::Parameters& origInv);
};
