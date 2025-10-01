// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include <queue>

#include <syst/context_group.h>
#include <syst/event.h>

#include "blockchain_monitor.h"
#include "logging/logger_ref.h"
#include "miner.h"
#include "miner_event.h"
#include "mining_config.h"

namespace syst
{
    class Dispatcher;
}

namespace miner
{

    class MinerManager
    {
    public:
        MinerManager(
            syst::Dispatcher &dispatcher,
            const mevacoin::MiningConfig &config,
            const std::shared_ptr<httplib::Client> httpClient);

        void start();

    private:
        syst::ContextGroup m_contextGroup;
        mevacoin::MiningConfig m_config;
        mevacoin::Miner m_miner;
        BlockchainMonitor m_blockchainMonitor;

        syst::Event m_eventOccurred;
        std::queue<MinerEvent> m_events;
        bool isRunning;

        mevacoin::BlockTemplate m_minedBlock;

        uint64_t m_lastBlockTimestamp;

        std::shared_ptr<httplib::Client> m_httpClient = nullptr;

        void eventLoop();
        MinerEvent waitEvent();
        void pushEvent(MinerEvent &&event);
        void printHashRate();

        void startMining(const mevacoin::BlockMiningParameters &params);
        void stopMining();

        void startBlockchainMonitoring();
        void stopBlockchainMonitoring();

        bool submitBlock(const mevacoin::BlockTemplate &minedBlock);
        mevacoin::BlockMiningParameters requestMiningParameters();

        void adjustBlockTemplate(mevacoin::BlockTemplate &blockTemplate) const;
    };

} // namespace miner
