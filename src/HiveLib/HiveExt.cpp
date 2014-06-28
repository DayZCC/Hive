/**
 * Copyright (C) 2009-2013 Rajko Stojadinovic <http://github.com/rajkosto/hive>
 * Andrew DeLisa <http://github.com/ayan4m1/hive>
 * Crosire <http://github.com/Crosire/DayZ-Private-Hive>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 **/

#include "HiveExt.h"
#include "Version.h"
#include "DataSource/DataSourceObject.h"
#include "DataSource/DataSourceCharacter.h"
#include <boost/bind.hpp>
#include <boost/optional.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/date_time/gregorian_calendar.hpp>
#include <boost/lexical_cast.hpp>
#include <Poco/RandomStream.h>
#include <Poco/HexBinaryEncoder.h>
#include <Poco/HexBinaryDecoder.h>

using boost::lexical_cast;
using boost::bad_lexical_cast;

/**
 * Main functions called from an SQF script
 **/

// Main Entry point
int HiveExtApp::main(const std::vector<std::string>& args)
{
	// Log down version
	logger().information("HiveExt " + VERSION + " [DayZ Server Controlcenter]");

	// Setup Timezone
	setupClock();

	if (!this->initialiseService())
	{
		logger().close();
		return EXIT_IOERR;
	}

	// Return success
	return EXIT_OK;
}
HiveExtApp::HiveExtApp(string suffixDir) : AppServer("HiveExt", suffixDir), _serverId(-1)
{
	// System
	handlers[307] = boost::bind(&HiveExtApp::getDateTime, this, _1);

	// Object update and streaming
	handlers[302] = boost::bind(&HiveExtApp::streamObjects, this, _1);
	handlers[305] = boost::bind(&HiveExtApp::vehicleMoved, this, _1);
	handlers[306] = boost::bind(&HiveExtApp::vehicleDamaged, this, _1);
	handlers[303] = boost::bind(&HiveExtApp::objectInventory, this, _1, false);
	handlers[309] = boost::bind(&HiveExtApp::objectInventory, this, _1, true);
	handlers[308] = boost::bind(&HiveExtApp::objectPublish, this, _1);
	handlers[304] = boost::bind(&HiveExtApp::objectDelete, this, _1, false);
	handlers[310] = boost::bind(&HiveExtApp::objectDelete, this, _1, true);

	// Character login
	handlers[101] = boost::bind(&HiveExtApp::playerLoad, this, _1);
	handlers[102] = boost::bind(&HiveExtApp::playerLoadDetails, this, _1);
	handlers[103] = boost::bind(&HiveExtApp::playerRecordLogin, this, _1);

	// Character updates
	handlers[201] = boost::bind(&HiveExtApp::playerUpdate, this, _1);
	handlers[202] = boost::bind(&HiveExtApp::playerDeath, this, _1);
	handlers[203] = boost::bind(&HiveExtApp::playerInit, this, _1);

	// Custom procedures
	handlers[998] = boost::bind(&HiveExtApp::customExecute, this, _1);
	handlers[999] = boost::bind(&HiveExtApp::streamCustom, this, _1);
}

// Extension Call Entry point
void HiveExtApp::callExtension(const char* function, char* output, size_t outputSize)
{
	Sqf::Parameters params;

	try
	{
		params = lexical_cast<Sqf::Parameters>(function);	
	}
	catch (bad_lexical_cast)
	{
		logger().error("Cannot parse function: " + string(function));
		return;
	}

	int funcNum = -1;

	try
	{
		string childIdent = boost::get<string>(params.at(0));

		if (childIdent != "CHILD")
		{
			throw std::runtime_error("First element in parameters must be CHILD");
		}

		params.erase(params.begin());
		funcNum = boost::get<int>(params.at(0));
		params.erase(params.begin());
	}
	catch (...)
	{
		logger().error("Invalid function format: " + string(function));
		return;
	}

	if (handlers.count(funcNum) < 1)
	{
		logger().error("Invalid method id: " + lexical_cast<string>(funcNum));
		return;
	}

	if (logger().debug())
	{
		logger().debug("Original params: |" + string(function) + "|");
	}

	logger().information("Method: " + lexical_cast<string>(funcNum) + " Params: " + lexical_cast<string>(params));
	
	HandlerFunc handler = handlers[funcNum];
	Sqf::Value res;
	boost::optional<ServerShutdownException> shutdownExc;

	try
	{
		res = handler(params);
	}
	catch (const ServerShutdownException& e)
	{
		if (!e.keyMatches(_initKey))
		{
			logger().error("Actually not shutting down");
			return;
		}

		shutdownExc = e;
		res = e.getReturnValue();
	}
	catch (...)
	{
		logger().error("Error executing |" + string(function) + "|");
		return;
	}		

	string serializedRes = lexical_cast<string>(res);
	logger().information("Result: " + serializedRes);

	if (serializedRes.length() >= outputSize)
	{
		logger().error("Output size too big (" + lexical_cast<string>(serializedRes.length()) + ") for request : " + string(function));
	}
	else
	{
		strncpy_s(output, outputSize, serializedRes.c_str(), outputSize-1);
	}

	if (shutdownExc.is_initialized())
	{
		throw *shutdownExc;
	}
}

// Generic functions
void HiveExtApp::setupClock()
{
	namespace pt = boost::posix_time;
	pt::ptime utc = pt::second_clock::universal_time();
	pt::ptime now;

	Poco::AutoPtr<Poco::Util::AbstractConfiguration> timeConf(config().createView("Time"));
	string timeType = timeConf->getString("Type", "Local");

	if (boost::iequals(timeType, "Custom"))
	{
		now = utc;

		const char* defOffset = "0";
		string offsetStr = timeConf->getString("Offset",defOffset);
		boost::trim(offsetStr);
		if (offsetStr.length() < 1) { offsetStr = defOffset; }
		
		try
		{
			now += pt::duration_from_string(offsetStr);
		}
		catch (const std::exception&)
		{
			logger().warning("Invalid value for Time.Offset configuration variable (expected int, given: " + offsetStr + ")");
		}
	}
	else if (boost::iequals(timeType, "Static"))
	{
		now = pt::second_clock::local_time();

		try
		{
			int hourOfTheDay = timeConf->getInt("Hour");
			now -= pt::time_duration(now.time_of_day().hours(), 0, 0);
			now += pt::time_duration(hourOfTheDay, 0, 0);
		}
		catch (const Poco::NotFoundException&)
		{
		}
		catch (const Poco::SyntaxException&) 
		{
			string hourStr = timeConf->getString("Hour", "");
			boost::trim(hourStr);
			if (hourStr.length() > 0)
				logger().warning("Invalid value for Time.Hour configuration variable (expected int, given: "+hourStr+")");
		}

		// Change the date
		{
			string dateStr = timeConf->getString("Date", "");
			boost::trim(dateStr);

			if (dateStr.length() > 0)
			{
				namespace gr = boost::gregorian;
				try
				{
					gr::date newDate = gr::from_uk_string(dateStr);
					now = pt::ptime(newDate, now.time_of_day());
				}
				catch (const std::exception&)
				{
					logger().warning("Invalid value for Time.Date configuration variable (expected date, given: "+dateStr+")");
				}
			}
		}
	}
	else
	{
		now = pt::second_clock::local_time();
	}

	_timeOffset = now - utc;
}
Sqf::Parameters ReturnStatus(bool isGood, string errorMsg = "")
	{
		Sqf::Parameters retVal;
		string retStatus = "PASS";

		if (!isGood)
		{
			retStatus = "ERROR";
		}

		retVal.push_back(retStatus);
		return retVal;
	}

/**
 * Custom functions executed when calling the extension
 **/

Sqf::Value HiveExtApp::getDateTime(Sqf::Parameters params)
{
	namespace pt=boost::posix_time;
	pt::ptime now = pt::second_clock::universal_time() + _timeOffset;

	Sqf::Parameters retVal;

	retVal.push_back(string("PASS"));
	{
		Sqf::Parameters dateTime;
		dateTime.push_back(static_cast<int>(now.date().year()));
		dateTime.push_back(static_cast<int>(now.date().month()));
		dateTime.push_back(static_cast<int>(now.date().day()));
		dateTime.push_back(static_cast<int>(now.time_of_day().hours()));
		dateTime.push_back(static_cast<int>(now.time_of_day().minutes()));
		retVal.push_back(dateTime);
	}

	return retVal;
}

Sqf::Value HiveExtApp::streamObjects(Sqf::Parameters params)
{
	if (_srvObjects.empty())
	{
		if (_initKey.length() < 1)
		{
			boost::array<UInt8, 16> keyData;
			Poco::RandomInputStream().read((char*)keyData.c_array(), keyData.size());
			std::ostringstream ostr;
			Poco::HexBinaryEncoder enc(ostr);
			enc.rdbuf()->setLineLength(0);
			enc.write((const char*)keyData.data(), keyData.size());
			enc.close();
			_initKey = ostr.str();
		}

		int serverId = boost::get<int>(params.at(0));
		setServerId(serverId);

		_objData->populateObjects(getServerId(), _srvObjects);

		Sqf::Parameters retVal;
		retVal.push_back(string("ObjectStreamStart"));
		retVal.push_back(static_cast<int>(_srvObjects.size()));
		retVal.push_back(_initKey);
		return retVal;
	}
	else
	{
		Sqf::Parameters retVal = _srvObjects.front();
		_srvObjects.pop();

		return retVal;
	}
}
Sqf::Value HiveExtApp::streamCustom(Sqf::Parameters params)
{
	if (_custQueue.empty())
	{
		string query = Sqf::GetStringAny(params.at(0));
		Sqf::Parameters rawParams = boost::get<Sqf::Parameters>(params.at(1));
		_customData->populateQuery(query, rawParams, _custQueue);
		Sqf::Parameters retVal;
		retVal.push_back(string("CustomStreamStart"));
		retVal.push_back(static_cast<int>(_custQueue.size()));

		return retVal;
	}
	else
	{
		Sqf::Parameters retVal = _custQueue.front();
		_custQueue.pop();

		return retVal;
	}
}

Sqf::Value HiveExtApp::objectInventory(Sqf::Parameters params, bool byUID /*= false*/)
{
	Int64 objectIdent = Sqf::GetBigInt(params.at(0));
	Sqf::Value inventory = boost::get<Sqf::Parameters>(params.at(1));

	if (objectIdent != 0) // All the vehicles have objectUID = 0, so it would be bad to update those
	{
		return ReturnStatus(_objData->updateObjectInventory(getServerId(), objectIdent, byUID, inventory));
	}

	return ReturnStatus(true);
}
Sqf::Value HiveExtApp::objectDelete(Sqf::Parameters params, bool byUID /*= false*/)
{
	Int64 objectIdent = Sqf::GetBigInt(params.at(0));

	if (objectIdent != 0) // All the vehicles have objectUID = 0, so it would be bad to delete those
	{
		return ReturnStatus(_objData->deleteObject(getServerId(), objectIdent, byUID));
	}

	return ReturnStatus(true);
}
Sqf::Value HiveExtApp::objectPublish(Sqf::Parameters params)
{
	int serverId = boost::get<int>(params.at(0));
	string className = boost::get<string>(params.at(1));
	int characterId = Sqf::GetIntAny(params.at(2));
	Sqf::Value worldSpace = boost::get<Sqf::Parameters>(params.at(3));
	Int64 uniqueId = Sqf::GetBigInt(params.at(4));

	return ReturnStatus(_objData->createObject(serverId, className, characterId, worldSpace, uniqueId));
}

Sqf::Value HiveExtApp::vehicleMoved(Sqf::Parameters params)
{
	Int64 objectIdent = Sqf::GetBigInt(params.at(0));
	Sqf::Value worldspace = boost::get<Sqf::Parameters>(params.at(1));
	double fuel = Sqf::GetDouble(params.at(2));

	if (objectIdent > 0) // Sometimes script sends this with object id 0, which is bad
	{
		return ReturnStatus(_objData->updateVehicleMovement(getServerId(), objectIdent, worldspace, fuel));
	}

	return ReturnStatus(true);
}
Sqf::Value HiveExtApp::vehicleDamaged(Sqf::Parameters params)
{
	Int64 objectIdent = Sqf::GetBigInt(params.at(0));
	Sqf::Value hitPoints = boost::get<Sqf::Parameters>(params.at(1));
	double damage = Sqf::GetDouble(params.at(2));

	if (objectIdent > 0) // Sometimes script sends this with object id 0, which is bad
	{
		return ReturnStatus(_objData->updateVehicleStatus(getServerId(), objectIdent, hitPoints, damage));
	}

	return ReturnStatus(true);
}

Sqf::Value HiveExtApp::playerLoad(Sqf::Parameters params)
{
	string playerId = Sqf::GetStringAny(params.at(0));
	string playerName = Sqf::GetStringAny(params.at(2));

	return _charData->fetchCharacterInitial(playerId, getServerId(), playerName);
}
Sqf::Value HiveExtApp::playerLoadDetails(Sqf::Parameters params)
{
	int characterId = Sqf::GetIntAny(params.at(0));
	
	return _charData->fetchCharacterDetails(characterId);
}
Sqf::Value HiveExtApp::playerRecordLogin(Sqf::Parameters params)
{
	string playerId = Sqf::GetStringAny(params.at(0));
	int characterId = Sqf::GetIntAny(params.at(1));
	int action = Sqf::GetIntAny(params.at(2));
	return ReturnStatus(_charData->recordLogEntry(playerId, characterId, getServerId(), action));
}

Sqf::Value HiveExtApp::playerUpdate(Sqf::Parameters params)
{
	const string nameIndex[] = {
		"character_id",
		"worldspace",
		"inventory",
		"backpack",
		"medical",
		"last_ate",
		"last_drank",
		"zombie_kills",
		"headshots",
		"distance",
		"survival_time",
		"state",
		"survivor_kills",
		"bandit_kills",
		"model",
		"humanity"
	};

	int characterId = Sqf::GetIntAny(params.at(0));
	CharDataSource::FieldsType fields;

	try
	{
		for (int i = 1; (i < params.size()) && (i <= 15); i++)
		{
			if (!Sqf::IsNull(params.at(i)))
			{
				if (i == 9)
				{
					continue;
				}

				//	Integers
				if (params.at(i).which() == 1)
				{
					int iValue = 0;

					if ((i == 10) || (i == 15))
					{
						iValue = static_cast<int>(Sqf::GetDouble(params.at(i)));
					} else {
						iValue = boost::get<int>(params.at(i));
					}

					if (((i == 15) && (iValue != 0)) || (iValue > 0))
					{
						fields[nameIndex[i]] = iValue;
					}
				}
				// Booleans
				else if(params.at(i).which() == 3)
				{
					bool bValue = boost::get<bool>(params.at(i));
					if(bValue) fields[nameIndex[i]] = true;
				}
				// Strings
				else if(params.at(i).which() == 4)
				{
					string sValue = boost::get<string>(params.at(i));
					fields[nameIndex[i]] = sValue;
				}
				// Arrays
				else if(params.at(i).which() == 6)
				{
					Sqf::Parameters sqfParam = boost::get<Sqf::Parameters>(params.at(i));
					
					if (sqfParam.size() > 0)
					{
						if (i == 4) {
							for (size_t j = 0; j< sqfParam.size(); j++) {
								if (Sqf::IsAny(sqfParam[j]))
								{
									logger().warning("update.medical[" + lexical_cast<string>(j) + "] changed from any to []");
									sqfParam[j] = Sqf::Parameters();
								}
							}
						}

						Sqf::Value sqfValue = sqfParam;
						fields[nameIndex[i]] = sqfValue;
					}
				}
			}
		}
	}
	catch (const std::out_of_range&)
	{
		logger().warning("Update of character " + lexical_cast<string>(characterId) + " only had " + lexical_cast<string>(params.size()) + " parameters out of 16");
	}

	if (fields.size() > 0)
	{
		return ReturnStatus(_charData->updateCharacter(characterId, fields));
	}

	return ReturnStatus(true);
}

Sqf::Value HiveExtApp::playerDeath(Sqf::Parameters params)
{
	int characterId = Sqf::GetIntAny(params.at(0));
	int duration = static_cast<int>(Sqf::GetDouble(params.at(1)));
	
	return ReturnStatus(_charData->killCharacter(characterId, duration));
}

Sqf::Value HiveExtApp::playerInit(Sqf::Parameters params) {
	int characterId = Sqf::GetIntAny(params.at(0));
	Sqf::Value inventory = boost::get<Sqf::Parameters>(params.at(1));
	Sqf::Value backpack = boost::get<Sqf::Parameters>(params.at(2));

	return ReturnStatus(_charData->initCharacter(characterId, inventory, backpack));
}

Sqf::Value HiveExtApp::customExecute(Sqf::Parameters params)
{
	string query = Sqf::GetStringAny(params.at(0));
	Sqf::Parameters rawParams = boost::get<Sqf::Parameters>(params.at(1));

	return _customData->customExecute(query, rawParams);
}