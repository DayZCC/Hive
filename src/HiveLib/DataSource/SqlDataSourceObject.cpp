#include "SqlDataSourceObject.h"
#include "Database/Database.h"
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <Poco/Util/AbstractConfiguration.h>

using boost::lexical_cast;
using boost::bad_lexical_cast;

namespace
{
	typedef boost::optional<Sqf::Value> PositionInfo;

	class PositionFixerVisitor : public boost::static_visitor<PositionInfo>
	{
		int max_x, max_y;
	public:
		explicit PositionFixerVisitor(const int x, const int y) : max_x(x), max_y(y) {}
		PositionInfo operator()(Sqf::Parameters& pos, int max_x, int max_y) const 
		{ 
			if (pos.size() != 3)
				return PositionInfo();

			try
			{
				double x = Sqf::GetDouble(pos[0]);
				double y = Sqf::GetDouble(pos[1]);
				double z = Sqf::GetDouble(pos[2]);

				if (x < max_x || y > max_y)
				{
					PositionInfo fixed(pos);
					pos.clear();
					return fixed;
				}
			}
			catch(const boost::bad_get&) {}

			return PositionInfo();
		}
		template<typename T> PositionInfo operator()(const T& other) const	{ return PositionInfo(); }
	};
	class WorldspaceFixerVisitor : public boost::static_visitor<PositionInfo>
	{
		int max_x, max_y;
	public:
		explicit WorldspaceFixerVisitor(const int x, const int y) : max_x(x), max_y(y) {}
		PositionInfo operator()(Sqf::Parameters& ws) const 
		{
			if (ws.size() != 2)
				return PositionInfo();

			return boost::apply_visitor(PositionFixerVisitor(max_x, max_y),ws[1]);
		}
		template<typename T> PositionInfo operator()(const T& other) const	{ return PositionInfo(); }
	};

	PositionInfo FixOOBWorldspace(Sqf::Value& v, const int max_x, const int max_y) { return boost::apply_visitor(WorldspaceFixerVisitor(max_x, max_y),v); }
};

SqlObjDataSource::SqlObjDataSource(Poco::Logger& logger, shared_ptr<Database> db, const Poco::Util::AbstractConfiguration* conf) : SqlDataSource(logger,db)
{
	_depTableName = getDB()->escape(conf->getString("Table", "instance_deployable"));
	_vehTableName = getDB()->escape(conf->getString("Table", "instance_vehicle"));
	_objectOOBReset = conf->getBool("ResetOOBObjects", false);
}

void SqlObjDataSource::populateObjects(int serverId, ServerObjectsQueue& queue)
{
	int max_x = 0;
	int max_y = 15360;

	if (_objectOOBReset)
	{
		auto limitRes = getDB()->queryParams("select `max_x`, `max_y` from `instance` where `id` = '%s'", serverId);
		
		if (limitRes && limitRes->fetchRow())
		{
			max_x = limitRes->at(0).getInt32();
			max_y = limitRes->at(1).getInt32();
		}
	}

	auto worldObjsRes = getDB()->queryParams("select iv.id, v.class_name, null owner_id, iv.worldspace, iv.inventory, iv.parts, iv.fuel, iv.damage, '0' from `%s` iv join `world_vehicle` wv on iv.`world_vehicle_id` = wv.`id` join `vehicle` v on wv.`vehicle_id` = v.`id` where iv.`instance_id` = %d union select id.`id`, d.`class_name`, id.`owner_id`, id.`worldspace`, id.`inventory`, '[]', 0, 0, id.`unique_id` from `%s` id inner join `deployable` d on id.`deployable_id` = d.`id` where id.`instance_id` = %d", _vehTableName.c_str(), serverId, _depTableName.c_str(), serverId);

	while (worldObjsRes->fetchRow())
	{
		auto row = worldObjsRes->fields();

		Sqf::Parameters objParams;
		objParams.push_back(string("OBJ"));

		int objectId = row[0].getInt32();
		objParams.push_back(lexical_cast<string>(objectId));

		try
		{
			objParams.push_back(row[1].getString()); // Classname
			objParams.push_back(lexical_cast<string>(row[2].getInt32())); // OwnerId

			Sqf::Value worldSpace = lexical_cast<Sqf::Value>(row[3].getString());
			if (_objectOOBReset)
			{
				PositionInfo posInfo = FixOOBWorldspace(worldSpace, max_x, max_y);

				if (posInfo.is_initialized())
				{
					_logger.information("Reset ObjectID " + lexical_cast<string>(objectId) + " (" + row[1].getString() + ") from position " + lexical_cast<string>(*posInfo));
				}

			}			
			objParams.push_back(worldSpace);

			// Inventory can be null
			{
				string invStr = "[]";

				if (!row[4].isNull())
				{
					invStr = row[4].getString();
				}

				objParams.push_back(lexical_cast<Sqf::Value>(invStr));
			}

			objParams.push_back(lexical_cast<Sqf::Value>(row[5].getCStr()));
			objParams.push_back(row[6].getDouble());
			objParams.push_back(row[7].getDouble());

			string uniqueId = "0";

			if (!row[8].isNull())
			{
				uniqueId = row[8].getString();
			}

			objParams.push_back(lexical_cast<string>(uniqueId));
		}
		catch (const bad_lexical_cast&)
		{
			_logger.error("Skipping ObjectID " + lexical_cast<string>(objectId) + " load because of invalid data in db");
			continue;
		}

		queue.push(objParams);
	}
}
bool SqlObjDataSource::updateObjectInventory(int serverId, Int64 objectIdent, bool byUID, const Sqf::Value& inventory)
{
	unique_ptr<SqlStatement> stmt;

	if (byUID)
	{
		// Deployable
		SqlStatementID stmt_id;
		stmt = getDB()->makeStatement(stmt_id, "update `" + _depTableName + "` set `inventory` = ? where `unique_id` = ? and `instance_id` = ?");
	}
	else
	{
		// Vehicle
		SqlStatementID stmt_id;
		stmt = getDB()->makeStatement(stmt_id, "update `" + _vehTableName + "` set `inventory` = ? where `id` = ? and `instance_id` = ?");
	}

	stmt->addString(lexical_cast<string>(inventory));
	stmt->addInt64(objectIdent);
	stmt->addInt32(serverId);

	bool exRes = stmt->execute();
	poco_assert(exRes == true);

	return exRes;
}
bool SqlObjDataSource::deleteObject(int serverId, Int64 objectIdent, bool byUID)
{
	unique_ptr<SqlStatement> stmt;

	if (byUID)
	{
		// Deployable
		SqlStatementID stmt_id;
		stmt = getDB()->makeStatement(stmt_id, "delete from `" + _depTableName + "` where `unique_id` = ? and `instance_id` = ?");
	}
	else
	{
		// Vehicle
		SqlStatementID stmt_id;
		stmt = getDB()->makeStatement(stmt_id, "delete from `" + _vehTableName + "` where `id` = ? and `instance_id` = ?");
	}

	stmt->addInt64(objectIdent);
	stmt->addInt32(serverId);

	bool exRes = stmt->execute();
	poco_assert(exRes == true);

	return exRes;
}
bool SqlObjDataSource::updateVehicleMovement(int serverId, Int64 objectIdent, const Sqf::Value& worldSpace, double fuel)
{
	SqlStatementID stmt_id;
	auto stmt = getDB()->makeStatement(stmt_id, "update `" + _vehTableName + "` set `worldspace` = ? , `fuel` = ? where `id` = ? and `instance_id` = ?");
	stmt->addString(lexical_cast<string>(worldSpace));
	stmt->addDouble(fuel);
	stmt->addInt64(objectIdent);
	stmt->addInt32(serverId);

	bool exRes = stmt->execute();
	poco_assert(exRes == true);

	return exRes;
}
bool SqlObjDataSource::updateVehicleStatus(int serverId, Int64 objectIdent, const Sqf::Value& hitPoints, double damage)
{
	SqlStatementID stmt_id;
	auto stmt = getDB()->makeStatement(stmt_id, "update `" + _vehTableName + "` set `parts` = ?, `damage` = ? where `id` = ? and `instance_id` = ?");
	stmt->addString(lexical_cast<string>(hitPoints));
	stmt->addDouble(damage);
	stmt->addInt64(objectIdent);
	stmt->addInt32(serverId);

	bool exRes = stmt->execute();
	poco_assert(exRes == true);

	return exRes;
}
bool SqlObjDataSource::createObject(int serverId, const string& className, int characterId, const Sqf::Value& worldSpace, Int64 objectIdent)
{
	SqlStatementID stmt_id;
	auto stmt = getDB()->makeStatement(stmt_id, "insert into `" + _depTableName + "` (`unique_id`, `deployable_id`, `owner_id`, `instance_id`, `worldspace`, `created`) select ?, d.id, ?, ?, ?, CURRENT_TIMESTAMP from deployable d where d.class_name = ?");
	stmt->addInt64(objectIdent);
	stmt->addInt32(characterId);
	stmt->addInt32(serverId);
	stmt->addString(lexical_cast<string>(worldSpace));
	stmt->addString(lexical_cast<string>(className));

	bool exRes = stmt->execute();
	poco_assert(exRes == true);

	return exRes;
}