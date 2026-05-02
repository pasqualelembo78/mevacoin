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

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "blockchain.hardforks"

// MevaCoin: hard fork progressivi a 1 blocco di distanza
// Ogni versione ha un'altezza DIVERSA (obbligatorio per Monero)
// I blocchi 1-11 usano CryptoNight (minati in secondi, difficoltà bassissima)
// Dal blocco 12 in poi: RandomX attivo (RX_BLOCK_VERSION = 12)
const hardfork_t mainnet_hard_forks[] = {
  { 1,  1,  0, 1341378000 },
  { 2,  2,  0, 1341378000 },
  { 3,  3,  0, 1341378000 },
  { 4,  4,  0, 1341378000 },
  { 5,  5,  0, 1341378000 },
  { 6,  6,  0, 1341378000 },
  { 7,  7,  0, 1341378000 },
  { 8,  8,  0, 1341378000 },
  { 9,  9,  0, 1341378000 },
  { 10, 10, 0, 1341378000 },
  { 11, 11, 0, 1341378000 },
  { 12, 12, 0, 1341378000 },  // RandomX PoW attivo dal blocco 12
  { 13, 13, 0, 1341378000 },
  { 14, 14, 0, 1341378000 },
  { 15, 15, 0, 1341378000 },
  { 16, 16, 0, 1341378000 },
};
const size_t num_mainnet_hard_forks = sizeof(mainnet_hard_forks) / sizeof(mainnet_hard_forks[0]);
const uint64_t mainnet_hard_fork_version_1_till = 0;

const hardfork_t testnet_hard_forks[] = {
  { 1,  1,  0, 1341378000 },
  { 2,  2,  0, 1341378000 },
  { 3,  3,  0, 1341378000 },
  { 4,  4,  0, 1341378000 },
  { 5,  5,  0, 1341378000 },
  { 6,  6,  0, 1341378000 },
  { 7,  7,  0, 1341378000 },
  { 8,  8,  0, 1341378000 },
  { 9,  9,  0, 1341378000 },
  { 10, 10, 0, 1341378000 },
  { 11, 11, 0, 1341378000 },
  { 12, 12, 0, 1341378000 },
  { 13, 13, 0, 1341378000 },
  { 14, 14, 0, 1341378000 },
  { 15, 15, 0, 1341378000 },
  { 16, 16, 0, 1341378000 },
};
const size_t num_testnet_hard_forks = sizeof(testnet_hard_forks) / sizeof(testnet_hard_forks[0]);
const uint64_t testnet_hard_fork_version_1_till = 0;

const hardfork_t stagenet_hard_forks[] = {
  { 1,  1,  0, 1341378000 },
  { 2,  2,  0, 1341378000 },
  { 3,  3,  0, 1341378000 },
  { 4,  4,  0, 1341378000 },
  { 5,  5,  0, 1341378000 },
  { 6,  6,  0, 1341378000 },
  { 7,  7,  0, 1341378000 },
  { 8,  8,  0, 1341378000 },
  { 9,  9,  0, 1341378000 },
  { 10, 10, 0, 1341378000 },
  { 11, 11, 0, 1341378000 },
  { 12, 12, 0, 1341378000 },
  { 13, 13, 0, 1341378000 },
  { 14, 14, 0, 1341378000 },
  { 15, 15, 0, 1341378000 },
  { 16, 16, 0, 1341378000 },
};
const size_t num_stagenet_hard_forks = sizeof(stagenet_hard_forks) / sizeof(stagenet_hard_forks[0]);
