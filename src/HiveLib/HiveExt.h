#pragma once

#include "Shared/Common/Types.h"
#include "Shared/Server/AppServer.h"
#include "Sqf.h"
#include "DataSource/DataSourceObject.h"
#include "DataSource/DataSourceCharacter.h"
#include "DataSource/DataSourceCustom.h"
#include <boost/function.hpp>
#include <boost/date_time.hpp>

class Database;
class HiveExtApp: public AppServer
{
public:
	HiveExtApp(string suffixDir);
	virtual ~HiveExtApp() {};

	struct ServerShutdownException : public std::exception
	{
		ServerShutdownException(string theKey, Sqf::Value theVal = false) : _theKey(std::move(theKey)), _theVal(std::move(theVal)) {}
		
		bool keyMatches(const string& otherKey) const 
		{ 
			return ((_theKey.length() > 0) && (_theKey == otherKey)); 
		}
		const Sqf::Value& getReturnValue() const { return _theVal; }
	private:
		string _theKey;
		Sqf::Value _theVal;
	};
	void callExtension(const char* function, char* output, size_t outputSize);

protected:
	int main(const std::vector<std::string>& args);
	virtual bool initialiseService() = 0;
	void setServerId(int newId) { _serverId = newId; }
	int getServerId() const { return _serverId; }
	unique_ptr<CharDataSource> _charData;
	unique_ptr<ObjDataSource> _objData;
	unique_ptr<CustomDataSource> _customData;
	string _initKey;

private:
	int _serverId;
	boost::posix_time::time_duration _timeOffset;
	void setupClock();

	typedef boost::function<Sqf::Value (Sqf::Parameters)> HandlerFunc;
	map<int,HandlerFunc> handlers;

	Sqf::Value getDateTime(Sqf::Parameters params);

	ObjDataSource::ServerObjectsQueue _srvObjects;
	CustomDataSource::CustomDataQueue _custQueue;
	Sqf::Value streamObjects(Sqf::Parameters params);
	Sqf::Value streamCustom(Sqf::Parameters params);

	Sqf::Value objectPublish(Sqf::Parameters params);
	Sqf::Value objectInventory(Sqf::Parameters params, bool byUID = false);
	Sqf::Value objectDelete(Sqf::Parameters params, bool byUID = false);

	Sqf::Value vehicleMoved(Sqf::Parameters params);
	Sqf::Value vehicleDamaged(Sqf::Parameters params);

	Sqf::Value playerLoad(Sqf::Parameters params);
	Sqf::Value playerLoadDetails(Sqf::Parameters params);
	Sqf::Value playerRecordLogin(Sqf::Parameters params);
	Sqf::Value playerUpdate(Sqf::Parameters params);
	Sqf::Value playerDeath(Sqf::Parameters params);
	Sqf::Value playerInit(Sqf::Parameters params);

	Sqf::Value dataRequest(Sqf::Parameters params, bool async = false);
	Sqf::Value dataStatus(Sqf::Parameters params);
	Sqf::Value dataFetchRow(Sqf::Parameters params);
	Sqf::Value dataClose(Sqf::Parameters params);

	Sqf::Value changeTableAccess(Sqf::Parameters params);
	Sqf::Value serverShutdown(Sqf::Parameters params);

	Sqf::Value customExecute(Sqf::Parameters params);
};