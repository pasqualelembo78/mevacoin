// Copyright (c) 2014-2024, The Monero Project
// Copyright (c) 2024, MevaCoin
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "hardforks.h"

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "blockchain.hardforks"

// MevaCoin Hard Fork Schedule
//
//   v1  - Blocco   1  : CryptoNight PoW (genesis)
//   v12 - Blocco  12  : RandomX PoW  (RX_BLOCK_VERSION = 12)
//   v13 - Blocco  13  : Participation Incentive System
//                       (badge, uptime rewards, Proof-of-Availability)
//   v14 - Blocco  14  : MevaTrust Consensus
//                       (BFT proposer election, FROST threshold signing, slashing)
//
// REGOLE: version, height, time devono essere TUTTI strettamente crescenti.
//
// HF_VERSION_MEVATRUST          = 13  (definito in src/cryptonote_config.h)
// HF_VERSION_MEVATRUST_CONSENSUS = 14
// Il sistema di incentivi e' attivo dal blocco 13 in avanti su tutte le reti.

const hardfork_t mainnet_hard_forks[] = {
  { 1,  1, 0, 1735689600 },   // v1  - CryptoNight (genesis)      2025-01-01 UTC
  { 12, 12, 0, 1748736000 },  // v12 - RandomX                    2025-06-01 UTC
  { 13, 13, 0, 1764547200 },  // v13 - Participation Incentive System   2025-12-01 UTC
  { 14, 14, 0, 1767225600 },  // v14 - MevaTrust Consensus             2026-01-01 UTC
};
const size_t num_mainnet_hard_forks = sizeof(mainnet_hard_forks) / sizeof(mainnet_hard_forks[0]);
const uint64_t mainnet_hard_fork_version_1_till = 0;

const hardfork_t testnet_hard_forks[] = {
  { 1,  1, 0, 1735689600 },
  { 12, 12, 0, 1748736000 },
  { 13, 13, 0, 1764547200 },  // v13 - Participation System (testnet)  2025-12-01 UTC
  { 14, 14, 0, 1767225600 },  // v14 - MevaTrust Consensus (testnet)   2026-01-01 UTC
};
const size_t num_testnet_hard_forks = sizeof(testnet_hard_forks) / sizeof(testnet_hard_forks[0]);
const uint64_t testnet_hard_fork_version_1_till = 0;

const hardfork_t stagenet_hard_forks[] = {
  { 1,  1, 0, 1341378000 },
  { 12, 12, 0, 1341378001 },
  { 13, 13, 0, 1341378002 },  // v13 - Participation System (stagenet)
  { 14, 14, 0, 1341378003 },  // v14 - MevaTrust Consensus (stagenet)
};
const size_t num_stagenet_hard_forks = sizeof(stagenet_hard_forks) / sizeof(stagenet_hard_forks[0]);
