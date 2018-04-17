#pragma once

#include <iostream>
#include <libdevcore/Worker.h>
#include <libvthcore/Farm.h>
#include <libvthcore/VthashAux.h>
#include <libvthcore/Miner.h>
#include "../PoolClient.h"

using namespace std;
using namespace dev;
using namespace vth;

class SimulateClient : public PoolClient, Worker
{
public:
	SimulateClient(unsigned const & difficulty, unsigned const & block);
	~SimulateClient();

	void connect() override;
	void disconnect() override;

	bool isConnected() override { return m_connected; }

	void submitHashrate(string const & rate) override;
	void submitSolution(Solution solution) override;

private:
	void workLoop() override;

	bool m_uppDifficulty = false;
	unsigned m_difficulty;
	unsigned m_block;
	std::chrono::steady_clock::time_point m_time;
};


