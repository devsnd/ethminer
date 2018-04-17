#pragma once

#include "ApiServer.h"
#include <libvthcore/Farm.h>
#include <libvthcore/Miner.h>
#include <jsonrpccpp/server/connectors/tcpsocketserver.h>

using namespace jsonrpc;
using namespace dev;
using namespace dev::vth;

class Api
{
public:
	Api(const int &port, Farm &farm);
private:
	ApiServer *m_server;
	Farm &m_farm;
};

