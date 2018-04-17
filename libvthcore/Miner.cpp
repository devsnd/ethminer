#include "Miner.h"
#include "VthashAux.h"

using namespace dev;
using namespace vth;

unsigned dev::vth::Miner::s_dagLoadMode = 0;

unsigned dev::vth::Miner::s_dagLoadIndex = 0;

unsigned dev::vth::Miner::s_dagCreateDevice = 0;

uint8_t* dev::vth::Miner::s_dagInHostMemory = NULL;

bool dev::vth::Miner::s_exit = false;

