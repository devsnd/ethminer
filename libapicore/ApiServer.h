#ifndef _APISERVER_H_
#define _APISERVER_H_

#include <libvthcore/Farm.h>
#include <libvthcore/Miner.h>
#include <jsonrpccpp/server.h>

using namespace jsonrpc;
using namespace dev;
using namespace dev::vth;
using namespace std::chrono;

class ApiServer : public AbstractServer<ApiServer>
{
public:
	ApiServer(AbstractServerConnector *conn, serverVersion_t type, Farm &farm, bool &readonly);
private:
	Farm &m_farm;
	void getMinerStat1(const Json::Value& request, Json::Value& response);
	void doMinerRestart(const Json::Value& request, Json::Value& response);
	void doMinerReboot(const Json::Value& request, Json::Value& response);
};

#endif //_APISERVER_H_