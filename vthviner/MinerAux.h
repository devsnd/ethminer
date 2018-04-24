#pragma once

/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file MinerAux.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * CLI module for mining.
 */

#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <random>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/optional.hpp>

#include <libvthcore/Exceptions.h>
#include <libdevcore/SHA3.h>
#include <libvthcore/VthashAux.h>
#include <libvthcore/Farm.h>
#if ETH_ETHASHCL
#include <libvthash-cl/CLVider.h>
#endif
#if ETH_ETHASHCUDA
#include <libvthash-cuda/CUDAViner.h>
#endif
#include <jsonrpccpp/client/connectors/httpclient.h>
#include "FarmClient.h"
#if ETH_STRATUM
#include <libvtratum/EthStratumClient.h>
#include <libvtratum/EthStratumClientV2.h>
#endif
#if ETH_DBUS
#include "DBusInt.h"
#endif
#if API_CORE
#include <libapicore/Api.h>
#endif

using namespace std;
using namespace dev;
using namespace dev::vth;


class BadArgument: public Exception {};
struct MiningChannel: public LogChannel
{
	static const char* name() { return EthGreen "  m"; }
	static const int verbosity = 2;
	static const bool debug = false;
};
#define minelog clog(MiningChannel)

inline std::string toJS(unsigned long _n)
{
	std::string h = toHex(toCompactBigEndian(_n, 1));
	// remove first 0, if it is necessary;
	std::string res = h[0] != '0' ? h : h.substr(1);
	return "0x" + res;
}

class VinerCLI
{
public:
	enum class OperationMode
	{
		None,
		Benchmark,
		Simulation,
		Farm,
		Stratum
	};

	VinerCLI(OperationMode _mode = OperationMode::None): mode(_mode) {}

	bool interpretOption(int& i, int argc, char** argv)
	{
		string arg = argv[i];
		if ((arg == "-F" || arg == "--farm") && i + 1 < argc)
		{
			mode = OperationMode::Farm;
			m_farmURL = argv[++i];
			m_activeFarmURL = m_farmURL;
		}
		else if ((arg == "-FF" || arg == "-SF" || arg == "-FS" || arg == "--farm-failover" || arg == "--vtratum-failover") && i + 1 < argc)
		{
			string url = argv[++i];

			if (mode == OperationMode::Stratum)
			{
				size_t p = url.find_last_of(":");
				if (p > 0)
				{
					m_farmFailOverURL = url.substr(0, p);
					if (p + 1 <= url.length())
						m_fport = url.substr(p + 1);
				}
				else
				{
					m_farmFailOverURL = url;
				}
			}
			else
			{
				m_farmFailOverURL = url;
			}
		}
		else if (arg == "--farm-recheck" && i + 1 < argc)
			try {
				m_farmRecheckSet = true;
				m_farmRecheckPeriod = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--farm-retries" && i + 1 < argc)
			try {
				m_maxFarmRetries = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
#if ETH_STRATUM
		else if ((arg == "-S" || arg == "--vtratum") && i + 1 < argc)
		{
			mode = OperationMode::Stratum;
			string url = string(argv[++i]);
			size_t p = url.find_last_of(":");
			if (p > 0)
			{
				m_farmURL = url.substr(0, p);
				if (p + 1 <= url.length())
					m_port = url.substr(p+1);
			}
			else
			{
				m_farmURL = url;
			}
		}
		else if ((arg == "-O" || arg == "--userpass") && i + 1 < argc)
		{
			string userpass = string(argv[++i]);
			size_t p = userpass.find_first_of(":");
			m_user = userpass.substr(0, p);
			if (p + 1 <= userpass.length())
				m_pass = userpass.substr(p+1);
		}
		else if ((arg == "-SC" || arg == "--vtratum-client") && i + 1 < argc)
		{
			try {
				m_vtratumClientVersion = atoi(argv[++i]);
				if (m_vtratumClientVersion > 2) m_vtratumClientVersion = 2;
				else if (m_vtratumClientVersion < 1) m_vtratumClientVersion = 1;
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if ((arg == "-SP" || arg == "--vtratum-protocol") && i + 1 < argc)
		{
			try {
				m_vtratumProtocol = atoi(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if ((arg == "-SE" || arg == "--vtratum-email") && i + 1 < argc)
		{
			try {
				m_email = string(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if ((arg == "-FO" || arg == "--failover-userpass") && i + 1 < argc)
		{
			string userpass = string(argv[++i]);
			size_t p = userpass.find_first_of(":");
			m_fuser = userpass.substr(0, p);
			if (p + 1 <= userpass.length())
				m_fpass = userpass.substr(p + 1);
		}
		else if ((arg == "-u" || arg == "--user") && i + 1 < argc)
		{
			m_user = string(argv[++i]);
		}
		else if ((arg == "-p" || arg == "--pass") && i + 1 < argc)
		{
			m_pass = string(argv[++i]);
		}
		else if ((arg == "-o" || arg == "--port") && i + 1 < argc)
		{
			m_port = string(argv[++i]);
		}
		else if ((arg == "-fu" || arg == "--failover-user") && i + 1 < argc)
		{
			m_fuser = string(argv[++i]);
		}
		else if ((arg == "-fp" || arg == "--failover-pass") && i + 1 < argc)
		{
			m_fpass = string(argv[++i]);
		}
		else if ((arg == "-fo" || arg == "--failover-port") && i + 1 < argc)
		{
			m_fport = string(argv[++i]);
		}
		else if ((arg == "--work-timeout") && i + 1 < argc)
		{
			m_worktimeout = atoi(argv[++i]);
		}
		else if ((arg == "-RH" || arg == "--report-vashrate"))
		{
			m_report_vtratum_vashrate = true;
		}
		else if ((arg == "-HWMON") && i + 1 < argc)
		{
			m_show_hwmonitors = true;
		}

#endif
#if API_CORE
		else if ((arg == "--api-port") && i + 1 < argc)
		{
			m_api_port = atoi(argv[++i]);
		}
#endif
#if ETH_ETHASHCL
		else if (arg == "--opencl-platform" && i + 1 < argc)
			try {
				m_openclPlatform = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--opencl-devices" || arg == "--opencl-device")
			while (m_openclDeviceCount < 16 && i + 1 < argc)
			{
				try
				{
					m_openclDevices[m_openclDeviceCount] = stol(argv[++i]);
					++m_openclDeviceCount;
				}
				catch (...)
				{
					i--;
					break;
				}
			}
		else if(arg == "--cl-parallel-hash" && i + 1 < argc) {
			try {
				m_openclThreadsPerHash = stol(argv[++i]);
				if(m_openclThreadsPerHash != 1 && m_openclThreadsPerHash != 2 &&
				   m_openclThreadsPerHash != 4 && m_openclThreadsPerHash != 8) {
					BOOST_THROW_EXCEPTION(BadArgument());
				} 
			}
			catch(...) {
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if (arg == "--cl-kernel" && i + 1 < argc)
		{
			try
			{
				m_openclSelectedKernel = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
#endif
#if ETH_ETHASHCL || ETH_ETHASHCUDA
		else if ((arg == "--cl-global-work" || arg == "--cuda-grid-size")  && i + 1 < argc)
			try {
				m_globalWorkSizeMultiplier = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if ((arg == "--cl-local-work" || arg == "--cuda-block-size") && i + 1 < argc)
			try {
				m_localWorkSize = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--list-devices")
			m_shouldListDevices = true;
#endif
#if ETH_ETHASHCUDA
		else if (arg == "--cuda-devices")
		{
			while (m_cudaDeviceCount < 16 && i + 1 < argc)
			{
				try
				{
					m_cudaDevices[m_cudaDeviceCount] = stol(argv[++i]);
					++m_cudaDeviceCount;
				}
				catch (...)
				{
					i--;
					break;
				}
			}
		}
                else if (arg == "--cuda-parallel-hash" && i + 1 < argc)
                {
                        try {
                                m_parallelHash = stol(argv[++i]);
                                if (m_parallelHash == 0 || m_parallelHash > 8)
                                {
                                    throw BadArgument();
                                }
                        }
                        catch (...)
                        {
                                cerr << "Bad " << arg << " option: " << argv[i] << endl;
                                BOOST_THROW_EXCEPTION(BadArgument());
                        }
                }
		else if (arg == "--cuda-schedule" && i + 1 < argc)
		{
			string mode = argv[++i];
			if (mode == "auto") m_cudaSchedule = 0;
			else if (mode == "spin") m_cudaSchedule = 1;
			else if (mode == "yield") m_cudaSchedule = 2;
			else if (mode == "sync") m_cudaSchedule = 4;
			else
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if (arg == "--cuda-streams" && i + 1 < argc)
			m_numStreams = stol(argv[++i]);
#endif
		else if ((arg == "-L" || arg == "--dag-load-mode") && i + 1 < argc)
		{
			string mode = argv[++i];
			if (mode == "parallel") m_dagLoadMode = DAG_LOAD_MODE_PARALLEL;
			else if (mode == "sequential") m_dagLoadMode = DAG_LOAD_MODE_SEQUENTIAL;
			else if (mode == "single")
			{
				m_dagLoadMode = DAG_LOAD_MODE_SINGLE;
				m_dagCreateDevice = stol(argv[++i]);
			}
			else
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if (arg == "--benchmark-warmup" && i + 1 < argc)
			try {
				m_benchmarkWarmup = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--benchmark-trial" && i + 1 < argc)
			try {
				m_benchmarkTrial = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--benchmark-trials" && i + 1 < argc)
			try
			{
				m_benchmarkTrials = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "-G" || arg == "--opencl")
			m_vinerType = MinerType::CL;
		else if (arg == "-U" || arg == "--cuda")
		{
			m_vinerType = MinerType::CUDA;
		}
		else if (arg == "-X" || arg == "--cuda-opencl")
		{
			m_vinerType = MinerType::Mixed;
		}
		else if (arg == "-M" || arg == "--benchmark")
		{
			mode = OperationMode::Benchmark;
			if (i + 1 < argc)
			{
				string m = boost::to_lower_copy(string(argv[++i]));
				try
				{
					m_benchmarkBlock = stol(m);
				}
				catch (...)
				{
					if (argv[i][0] == 45) { // check next arg
						i--;
					}
					else {
						cerr << "Bad " << arg << " option: " << argv[i] << endl;
						BOOST_THROW_EXCEPTION(BadArgument());
					}
				}
			}
		}
		else if (arg == "-Z" || arg == "--simulation") {
			mode = OperationMode::Simulation;
			if (i + 1 < argc)
			{
				string m = boost::to_lower_copy(string(argv[++i]));
				try
				{
					m_benchmarkBlock = stol(m);
				}
				catch (...)
				{
					if (argv[i][0] == 45) { // check next arg
						i--;
					}
					else {
						cerr << "Bad " << arg << " option: " << argv[i] << endl;
						BOOST_THROW_EXCEPTION(BadArgument());
					}
				}
			}
		}
		else if ((arg == "-t" || arg == "--mining-threads") && i + 1 < argc)
		{
			try
			{
				m_miningThreads = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else
			return false;
		return true;
	}

	void execute()
	{
		if (m_shouldListDevices)
		{
#if ETH_ETHASHCL
			if (m_vinerType == MinerType::CL || m_vinerType == MinerType::Mixed)
				CLVider::listDevices();
#endif
#if ETH_ETHASHCUDA
			if (m_vinerType == MinerType::CUDA || m_vinerType == MinerType::Mixed)
				CUDAViner::listDevices();
#endif
			exit(0);
		}

		if (m_vinerType == MinerType::CL || m_vinerType == MinerType::Mixed)
		{
#if ETH_ETHASHCL
			if (m_openclDeviceCount > 0)
			{
				CLVider::setDevices(m_openclDevices, m_openclDeviceCount);
				m_miningThreads = m_openclDeviceCount;
			}
			
			CLVider::setCLKernel(m_openclSelectedKernel);
			CLVider::setThreadsPerHash(m_openclThreadsPerHash);

			if (!CLVider::configureGPU(
					m_localWorkSize,
					m_globalWorkSizeMultiplier,
					m_openclPlatform,
					0,
					m_dagLoadMode,
					m_dagCreateDevice
				))
				exit(1);
			CLVider::setNumInstances(m_miningThreads);
#else
			cerr << "Selected GPU mining without having compiled with -DETHASHCL=1" << endl;
			exit(1);
#endif
		}
		if (m_vinerType == MinerType::CUDA || m_vinerType == MinerType::Mixed)
		{
#if ETH_ETHASHCUDA
			if (m_cudaDeviceCount > 0)
			{
				CUDAViner::setDevices(m_cudaDevices, m_cudaDeviceCount);
				m_miningThreads = m_cudaDeviceCount;
			}

			CUDAViner::setNumInstances(m_miningThreads);
			if (!CUDAViner::configureGPU(
				m_localWorkSize,
				m_globalWorkSizeMultiplier,
				m_numStreams,
				m_cudaSchedule,
				0,
				m_dagLoadMode,
				m_dagCreateDevice
				))
				exit(1);

			CUDAViner::setParallelHash(m_parallelHash);
#else
			cerr << "CUDA support disabled. Configure project build with -DETHASHCUDA=ON" << endl;
			exit(1);
#endif
		}
		if (mode == OperationMode::Benchmark)
			doBenchmark(m_vinerType, m_benchmarkWarmup, m_benchmarkTrial, m_benchmarkTrials);
		else if (mode == OperationMode::Farm)
			doFarm(m_vinerType, m_activeFarmURL, m_farmRecheckPeriod);
		else if (mode == OperationMode::Simulation)
			doSimulation(m_vinerType);
#if ETH_STRATUM
		else if (mode == OperationMode::Stratum)
			doStratum();
#endif
	}

	static void streamHelp(ostream& _out)
	{
		_out




#if ETH_STRATUM














#endif
			<< endl




















#if ETH_ETHASHCL




//			<< "        2: experimental kernel" << endl



#endif
#if ETH_ETHASHCUDA











#endif
#if API_CORE


#endif
			;
	}

private:

	void doBenchmark(MinerType _m, unsigned _warmupDuration = 15, unsigned _trialDuration = 3, unsigned _trials = 5)
	{
		BlockHeader genesis;
		genesis.setNumber(m_benchmarkBlock);
		genesis.setDifficulty(1 << 18);
		cdebug << genesis.boundary();

		Farm f;
		f.set_pool_addresses(m_farmURL, m_port, m_farmFailOverURL, m_fport);
		map<string, Farm::SealerDescriptor> sealers;
#if ETH_ETHASHCL
		sealers["opencl"] = Farm::SealerDescriptor{&CLVider::instances, [](FarmFace& _farm, unsigned _index){ return new CLVider(_farm, _index); }};
#endif
#if ETH_ETHASHCUDA
		sealers["cuda"] = Farm::SealerDescriptor{ &CUDAViner::instances, [](FarmFace& _farm, unsigned _index){ return new CUDAViner(_farm, _index); } };
#endif
		f.setSealers(sealers);
		f.onSolutionFound([&](Solution) { return false; });

		string platformInfo = _m == MinerType::CL ? "CL" : "CUDA";
		cout << "Benchmarking on platform: " << platformInfo << endl;

		cout << "Preparing DAG for block #" << m_benchmarkBlock << endl;
		//genesis.prep();

		genesis.setDifficulty(u256(1) << 63);
		if (_m == MinerType::CL)
			f.start("opencl", false);
		else if (_m == MinerType::CUDA)
			f.start("cuda", false);
		f.setWork(WorkPackage{genesis});

		map<uint64_t, WorkingProgress> results;
		uint64_t mean = 0;
		uint64_t innerMean = 0;
		for (unsigned i = 0; i <= _trials; ++i)
		{
			if (!i)
				cout << "Warming up..." << endl;
			else
				cout << "Trial " << i << "... " << flush;
			this_thread::sleep_for(chrono::seconds(i ? _trialDuration : _warmupDuration));

			auto mp = f.miningProgress();
			if (!i)
				continue;
			auto rate = mp.rate();

			cout << rate << endl;
			results[rate] = mp;
			mean += rate;
		}
		f.stop();
		int j = -1;
		for (auto const& r: results)
			if (++j > 0 && j < (int)_trials - 1)
				innerMean += r.second.rate();
		innerMean /= (_trials - 2);
		cout << "min/mean/max: " << results.begin()->second.rate() << "/" << (mean / _trials) << "/" << results.rbegin()->second.rate() << " H/s" << endl;
		cout << "inner mean: " << innerMean << " H/s" << endl;

		exit(0);
	}

	void doSimulation(MinerType _m, int difficulty = 20)
	{
		BlockHeader genesis;
		genesis.setNumber(m_benchmarkBlock);
		genesis.setDifficulty(1 << 18);
		cdebug << genesis.boundary();

		Farm f;
		f.set_pool_addresses(m_farmURL, m_port, m_farmFailOverURL, m_fport);
		map<string, Farm::SealerDescriptor> sealers;
#if ETH_ETHASHCL
		sealers["opencl"] = Farm::SealerDescriptor{ &CLVider::instances, [](FarmFace& _farm, unsigned _index){ return new CLVider(_farm, _index); } };
#endif
#if ETH_ETHASHCUDA
		sealers["cuda"] = Farm::SealerDescriptor{ &CUDAViner::instances, [](FarmFace& _farm, unsigned _index){ return new CUDAViner(_farm, _index); } };
#endif
		f.setSealers(sealers);

		string platformInfo = _m == MinerType::CL ? "CL" : "CUDA";
		cout << "Running mining simulation on platform: " << platformInfo << endl;

		cout << "Preparing DAG for block #" << m_benchmarkBlock << endl;
		//genesis.prep();

		genesis.setDifficulty(u256(1) << difficulty);

		if (_m == MinerType::CL)
			f.start("opencl", false);
		else if (_m == MinerType::CUDA)
			f.start("cuda", false);

		int time = 0;

		WorkPackage current = WorkPackage(genesis);
		f.setWork(current);
		while (true) {
			bool completed = false;
			Solution solution;
			f.onSolutionFound([&](Solution sol)
			{
				solution = sol;
				return completed = true;
			});
			for (unsigned i = 0; !completed; ++i)
			{
				auto mp = f.miningProgress();

				cnote << "Mining on difficulty " << difficulty << " " << mp;
				this_thread::sleep_for(chrono::milliseconds(1000));
				time++;
			}
			cnote << "Difficulty:" << difficulty << "  Nonce:" << solution.nonce;
			if (VthashAux::eval(current.seed, current.header, solution.nonce).value < current.boundary)
			{
				cnote << "SUCCESS: GPU gave correct result!";
			}
			else
				cwarn << "FAILURE: GPU gave incorrect result!";

			if (time < 12)
				difficulty++;
			else if (time > 18)
				difficulty--;
			time = 0;
			genesis.setDifficulty(u256(1) << difficulty);
			genesis.noteDirty();

			current.header = h256::random();
			current.boundary = genesis.boundary();
			minelog << "Generated random work package:";
			minelog << "  Header-hash:" << current.header.hex();
			minelog << "  Seedhash:" << current.seed.hex();
			minelog << "  Target: " << h256(current.boundary).hex();
			f.setWork(current);

		}
	}


	void doFarm(MinerType _m, string & _remote, unsigned _recheckPeriod)
	{
		map<string, Farm::SealerDescriptor> sealers;
#if ETH_ETHASHCL
		sealers["opencl"] = Farm::SealerDescriptor{&CLVider::instances, [](FarmFace& _farm, unsigned _index){ return new CLVider(_farm, _index); }};
#endif
#if ETH_ETHASHCUDA
		sealers["cuda"] = Farm::SealerDescriptor{ &CUDAViner::instances, [](FarmFace& _farm, unsigned _index){ return new CUDAViner(_farm, _index); } };
#endif
		(void)_m;
		(void)_remote;
		(void)_recheckPeriod;
		jsonrpc::HttpClient client(m_farmURL);
		:: FarmClient rpc(client);
		jsonrpc::HttpClient failoverClient(m_farmFailOverURL);
		::FarmClient rpcFailover(failoverClient);

		FarmClient * prpc = &rpc;

		h256 id = h256::random();
		Farm f;
		f.set_pool_addresses(m_farmURL, m_port, m_farmFailOverURL, m_fport);
		
#if API_CORE
		Api api(this->m_api_port, f);
#endif
		
		f.setSealers(sealers);

		if (_m == MinerType::CL)
			f.start("opencl", false);
		else if (_m == MinerType::CUDA)
			f.start("cuda", false);
		else if (_m == MinerType::Mixed) {
			f.start("cuda", false);
			f.start("opencl", true);
		}

		WorkPackage current;
		std::mutex x_current;
		while (m_running)
			try
			{
				bool completed = false;
				Solution solution;
				f.onSolutionFound([&](Solution sol)
				{
					solution = sol;
					return completed = true;
				});
				for (unsigned i = 0; !completed; ++i)
				{
					auto mp = f.miningProgress(m_show_hwmonitors);
					if (current)
					{
						minelog << mp << f.getSolutionStats() << f.farmLaunchedFormatted();
#if ETH_DBUS
						dbusint.send(toString(mp).data());
#endif
					}
					else
						minelog << "Waiting for work package...";

					auto rate = mp.rate();

					try
					{
						prpc->eth_submitHashrate(toJS(rate), "0x" + id.hex());
					}
					catch (jsonrpc::JsonRpcException const& _e)
					{
						cwarn << "Failed to submit vashrate.";
						cwarn << boost::diagnostic_information(_e);
					}

					Json::Value v = prpc->eth_getWork();
					h256 hh(v[0].asString());
					h256 newSeedHash(v[1].asString());

					if (hh != current.header)
					{
						x_current.lock();
						current.header = hh;
						current.seed = newSeedHash;
						current.boundary = h256(fromHex(v[2].asString()), h256::AlignRight);
						minelog << "Got work package: #" + current.header.hex().substr(0,8);
						f.setWork(current);
						x_current.unlock();
					}
					this_thread::sleep_for(chrono::milliseconds(_recheckPeriod));
				}
				bool ok = prpc->eth_submitWork("0x" + toHex(solution.nonce), "0x" + toString(solution.headerHash), "0x" + toString(solution.mixHash));
				if (ok) {
					cnote << "Solution found; Submitted to" << _remote;
					cnote << "  Nonce:" << solution.nonce;
					cnote << "  headerHash:" << solution.headerHash.hex();
					cnote << "  mixHash:" << solution.mixHash.hex();
					cnote << EthLime << " Accepted." << EthReset;
					f.acceptedSolution(solution.stale);
				}
				else {
					cwarn << "Solution found; Submitted to" << _remote;
					cwarn << "  Nonce:" << solution.nonce;
					cwarn << "  headerHash:" << solution.headerHash.hex();
					cwarn << "  mixHash:" << solution.mixHash.hex();
					cwarn << EthYellow << " Rejected." << EthReset;
					f.rejectedSolution(solution.stale);
				}
			}
			catch (jsonrpc::JsonRpcException&)
			{
				if (m_maxFarmRetries > 0)
				{
					for (auto i = 3; --i; this_thread::sleep_for(chrono::seconds(1)))
						cerr << "JSON-RPC problem. Probably couldn't connect. Retrying in " << i << "... \r";
					cerr << endl;
				}
				else
				{
					cerr << "JSON-RPC problem. Probably couldn't connect." << endl;
				}
				if (m_farmFailOverURL != "")
				{
					m_farmRetries++;
					if (m_farmRetries > m_maxFarmRetries)
					{
						if (_remote == "exit")
						{
							m_running = false;
						}
						else if (_remote == m_farmURL) {
							_remote = m_farmFailOverURL;
							prpc = &rpcFailover;
						}
						else {
							_remote = m_farmURL;
							prpc = &rpc;
						}
						m_farmRetries = 0;
					}

				}
			}
		exit(0);
	}

#if ETH_STRATUM
	void doStratum()
	{
		map<string, Farm::SealerDescriptor> sealers;
#if ETH_ETHASHCL
		sealers["opencl"] = Farm::SealerDescriptor{ &CLVider::instances, [](FarmFace& _farm, unsigned _index){ return new CLVider(_farm, _index); } };
#endif
#if ETH_ETHASHCUDA
		sealers["cuda"] = Farm::SealerDescriptor{ &CUDAViner::instances, [](FarmFace& _farm, unsigned _index){ return new CUDAViner(_farm, _index); } };
#endif
		if (!m_farmRecheckSet)
			m_farmRecheckPeriod = m_defaultStratumFarmRecheckPeriod;

		Farm f;
		f.set_pool_addresses(m_farmURL, m_port, m_farmFailOverURL, m_fport);
		
#if API_CORE
		Api api(this->m_api_port, f);
#endif
	
		// this is very ugly, but if Stratum Client V2 tunrs out to be a success, V1 will be completely removed anyway
		if (m_vtratumClientVersion == 1) {
			EthStratumClient client(&f, m_vinerType, m_farmURL, m_port, m_user, m_pass, m_maxFarmRetries, m_worktimeout, m_vtratumProtocol, m_email);
			if (m_farmFailOverURL != "")
			{
				if (m_fuser != "")
				{
					client.setFailover(m_farmFailOverURL, m_fport, m_fuser, m_fpass);
				}
				else
				{
					client.setFailover(m_farmFailOverURL, m_fport);
				}
			}
			f.setSealers(sealers);

			f.onSolutionFound([&](Solution sol)
			{
				if (client.isConnected()) {
					client.submit(sol);
				}
				else {
					cwarn << "Can't submit solution: Not connected";
				}
				return false;
			});
			f.onMinerRestart([&](){ 
				client.reconnect();
			});

			while (client.isRunning())
			{
				auto mp = f.miningProgress(m_show_hwmonitors);
				if (client.isConnected())
				{
					if (client.current())
					{
						minelog << mp << f.getSolutionStats() << f.farmLaunchedFormatted();
#if ETH_DBUS
						dbusint.send(toString(mp).data());
#endif
					}
					else
					{
						minelog << "Waiting for work package...";
					}
					
					if (this->m_report_vtratum_vashrate) {
						auto rate = mp.rate();
						client.submitHashrate(toJS(rate));
					}
				}
				this_thread::sleep_for(chrono::milliseconds(m_farmRecheckPeriod));
			}
		}
		else if (m_vtratumClientVersion == 2) {
			EthStratumClientV2 client(&f, m_vinerType, m_farmURL, m_port, m_user, m_pass, m_maxFarmRetries, m_worktimeout, m_vtratumProtocol, m_email);
			if (m_farmFailOverURL != "")
			{
				if (m_fuser != "")
				{
					client.setFailover(m_farmFailOverURL, m_fport, m_fuser, m_fpass);
				}
				else
				{
					client.setFailover(m_farmFailOverURL, m_fport);
				}
			}
			f.setSealers(sealers);

			f.onSolutionFound([&](Solution sol)
			{
				client.submit(sol);
				return false;
			});
			f.onMinerRestart([&](){ 
				client.reconnect();
			});

			while (client.isRunning())
			{
				auto mp = f.miningProgress(m_show_hwmonitors);
				if (client.isConnected())
				{
					if (client.current())
					{
						minelog << mp << f.getSolutionStats();
#if ETH_DBUS
						dbusint.send(toString(mp).data());
#endif
					}
					else if (client.waitState() == MINER_WAIT_STATE_WORK)
					{
						minelog << "Waiting for work package...";
					}
					
					if (this->m_report_vtratum_vashrate) {
						auto rate = mp.rate();
						client.submitHashrate(toJS(rate));
					}
				}
				this_thread::sleep_for(chrono::milliseconds(m_farmRecheckPeriod));
			}
		}

	}
#endif

	/// Operating mode.
	OperationMode mode;

	/// Mining options
	bool m_running = true;
	MinerType m_vinerType = MinerType::Mixed;
	unsigned m_openclPlatform = 0;
	unsigned m_miningThreads = UINT_MAX;
	bool m_shouldListDevices = false;
#if ETH_ETHASHCL
	unsigned m_openclSelectedKernel = 0;  ///< A numeric value for the selected OpenCL kernel
	unsigned m_openclDeviceCount = 0;
	unsigned m_openclDevices[16];
	unsigned m_openclThreadsPerHash = 8;
#if !ETH_ETHASHCUDA
	unsigned m_globalWorkSizeMultiplier = CLVider::c_defaultGlobalWorkSizeMultiplier;
	unsigned m_localWorkSize = CLVider::c_defaultLocalWorkSize;
#endif
#endif
#if ETH_ETHASHCUDA
	unsigned m_globalWorkSizeMultiplier = CUDAViner::c_defaultGridSize;
	unsigned m_localWorkSize = CUDAViner::c_defaultBlockSize;
	unsigned m_cudaDeviceCount = 0;
	unsigned m_cudaDevices[16];
	unsigned m_numStreams = CUDAViner::c_defaultNumStreams;
	unsigned m_cudaSchedule = 4; // sync
#endif
	unsigned m_dagLoadMode = 0; // parallel
	unsigned m_dagCreateDevice = 0;
	/// Benchmarking params
	unsigned m_benchmarkWarmup = 15;
	unsigned m_parallelHash    = 4;
	unsigned m_benchmarkTrial = 3;
	unsigned m_benchmarkTrials = 5;
	unsigned m_benchmarkBlock = 0;
	/// Farm params
	string m_farmURL = "http://127.0.0.1:8545";
	string m_farmFailOverURL = "";


	string m_activeFarmURL = m_farmURL;
	unsigned m_farmRetries = 0;
	unsigned m_maxFarmRetries = 3;
	unsigned m_farmRecheckPeriod = 500;
	unsigned m_defaultStratumFarmRecheckPeriod = 2000;
	bool m_farmRecheckSet = false;
	int m_worktimeout = 180;
	bool m_show_hwmonitors = false;
#if API_CORE
	int m_api_port = 0;
#endif	

#if ETH_STRATUM
	bool m_report_vtratum_vashrate = false;
	int m_vtratumClientVersion = 1;
	int m_vtratumProtocol = STRATUM_PROTOCOL_STRATUM;
	string m_user;
	string m_pass;
	string m_port;
	string m_fuser = "";
	string m_fpass = "";
	string m_email = "";
#endif
	string m_fport = "";

#if ETH_DBUS
	DBusInt dbusint;
#endif
};
