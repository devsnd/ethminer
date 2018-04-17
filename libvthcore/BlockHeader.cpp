/// vthviner -- Ethereum viner with OpenCL, CUDA and vtratum support.
/// Copyright 2018 vthviner Authors.
/// Licensed under GNU General Public License, Version 3. See the LICENSE file.

#include "BlockHeader.h"
#include "VthashAux.h"
#include <libdevcore/Common.h>
#include <libdevcore/Log.h>
#include <libdevcore/RLP.h>

namespace dev
{
namespace vth
{
h256 const& BlockHeader::boundary() const
{
    if (!m_boundary && m_difficulty)
        m_boundary = (h256)(u256)((bigint(1) << 256) / m_difficulty);
    return m_boundary;
}

h256 const& BlockHeader::hashWithout() const
{
    if (!m_hashWithout)
    {
        RLPStream s(BasicFields);
        streamRLPFields(s);
        m_hashWithout = sha3(s.out());
    }
    return m_hashWithout;
}

void BlockHeader::streamRLPFields(RLPStream& _s) const
{
    _s << m_parentHash << m_sha3Uncles << m_coinbaseAddress << m_stateRoot << m_transactionsRoot
       << m_receiptsRoot << m_logBloom << m_difficulty << m_number << m_gasLimit << m_gasUsed
       << m_timestamp << m_extraData;
}
}  // namespace vth
}  // namespace dev
