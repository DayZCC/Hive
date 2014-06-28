#pragma once

#include "SqlDataSource.h"
#include "DataSourceCharacter.h"
#include "Database/SqlStatement.h"

class SqlCharDataSource : public SqlDataSource, public CharDataSource
{
public:
	SqlCharDataSource(Poco::Logger& logger, shared_ptr<Database> db, const string& idFieldName, const string& wsFieldName);
	~SqlCharDataSource();

	Sqf::Value fetchCharacterInitial( string playerId, int serverId, const string& playerName ) override;
	Sqf::Value fetchCharacterDetails( int characterId ) override;
	bool updateCharacter( int characterId, const FieldsType& fields ) override;
	bool killCharacter( int characterId, int duration ) override;
	bool initCharacter( int characterId, const Sqf::Value& inventory, const Sqf::Value& backpack ) override;
	bool recordLogEntry( string playerId, int characterId, int serverId, int action ) override;

private:
	string _idFieldName;
	string _wsFieldName;
};