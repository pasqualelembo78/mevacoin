// Copyright (c) 2025, The Mevacoin Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <limits>
#include <initializer_list>
#include <boost/uuid/uuid.hpp>

namespace mevacoin
{
    namespace parameters
    {

        const uint64_t DIFFICULTY_TARGET = 180; // seconds

        const uint32_t MEVACOIN_MAX_BLOCK_NUMBER = 500000000;
        const size_t MEVACOIN_MAX_BLOCK_BLOB_SIZE = 500000000;
        const size_t MEVACOIN_MAX_TX_SIZE = 1000000000;
        const uint64_t MEVACOIN_PUBLIC_ADDRESS_BASE58_PREFIX = 18511;
        const uint32_t MEVACOIN_MINED_MONEY_UNLOCK_WINDOW = 20;
        const uint64_t MEVACOIN_BLOCK_FUTURE_TIME_LIMIT = 60 * 60 * 2;
        const uint64_t MEVACOIN_BLOCK_FUTURE_TIME_LIMIT_V3 = 3 * DIFFICULTY_TARGET;
        const uint64_t MEVACOIN_BLOCK_FUTURE_TIME_LIMIT_V4 = 6 * DIFFICULTY_TARGET;

        const size_t BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW = 60;
        const size_t BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW_V3 = 11;

        // MONEY_SUPPLY - total number coins to be generated
        const uint64_t MONEY_SUPPLY = UINT64_C(10000000000000000);
        const uint32_t ZAWY_DIFFICULTY_BLOCK_INDEX = 187000;
        const size_t ZAWY_DIFFICULTY_V2 = 0;
        const uint8_t ZAWY_DIFFICULTY_DIFFICULTY_BLOCK_VERSION = 3;

        const uint64_t LWMA_2_DIFFICULTY_BLOCK_INDEX = 620000;
        const uint64_t LWMA_2_DIFFICULTY_BLOCK_INDEX_V2 = 700000;
        const uint64_t LWMA_2_DIFFICULTY_BLOCK_INDEX_V3 = 800000;

        const uint64_t LWMA_3_DIFFICULTY_BLOCK_INDEX = 2200000;

        const unsigned EMISSION_SPEED_FACTOR = 28;
        static_assert(EMISSION_SPEED_FACTOR <= 8 * sizeof(uint64_t), "Bad EMISSION_SPEED_FACTOR");

        /* Premine amount */
        const uint64_t GENESIS_BLOCK_REWARD = UINT64_C(0);

        /* How to generate a premine:

        * Compile your code

        * Run zedwallet, ignore that it can't connect to the daemon, and generate an
          address. Save this and the keys somewhere safe.

        * Launch the daemon with these arguments:
        --print-genesis-tx --genesis-block-reward-address <premine wallet address>

        For example:
       ./mevacoind --print-genesis-tx --genesis-block-reward-address bickyEqYy97PXRdgsLhwea2ojYW3FzCcBS6ir4jYuS645QH64Cuv4GGVKuFiTi75nyUtQiYDKaivqKSBJHEPgX752s1sjZHTFt

        * Take the hash printed, and replace it with the hash below in GENESIS_COINBASE_TX_HEX

        * Recompile, setup your seed nodes, and start mining

        * You should see your premine appear in the previously generated wallet.

        */
        const char GENESIS_COINBASE_TX_HEX[] = "011401ff000100024e8627e5eb3afae39f847029dda8986de3175e6ec043317a0c45065f4a5898c221011616e1e1a1f7bfe50cd57213f01bfde3cad7813181bf2e940d34ed8de42c9d46";
        static_assert(sizeof(GENESIS_COINBASE_TX_HEX) / sizeof(*GENESIS_COINBASE_TX_HEX) != 1, "GENESIS_COINBASE_TX_HEX must not be empty.");

        /* This is the unix timestamp of the first "mined" block (technically block 2, not the genesis block)
           You can get this value by doing "print_block 2" in TurtleCoind. It is used to know what timestamp
           to import from when the block height cannot be found in the node or the node is offline. */
        const uint64_t GENESIS_BLOCK_TIMESTAMP = 1729612800;

        const size_t MEVACOIN_REWARD_BLOCKS_WINDOW = 100;
        const size_t MEVACOIN_BLOCK_GRANTED_FULL_REWARD_ZONE = 100000; // size of block (bytes) after which reward for block calculated using block size
        const size_t MEVACOIN_BLOCK_GRANTED_FULL_REWARD_ZONE_V2 = 20000;
        const size_t MEVACOIN_BLOCK_GRANTED_FULL_REWARD_ZONE_V1 = 10000;
        const size_t MEVACOIN_BLOCK_GRANTED_FULL_REWARD_ZONE_CURRENT = MEVACOIN_BLOCK_GRANTED_FULL_REWARD_ZONE;
        const size_t MEVACOIN_COINBASE_BLOB_RESERVED_SIZE = 600;

        const size_t MEVACOIN_DISPLAY_DECIMAL_POINT = 5;

        const uint64_t MINIMUM_FEE = UINT64_C(10);

        /* This section defines our minimum and maximum mixin counts required for transactions */
        const uint64_t MINIMUM_MIXIN_V1 = 0;
        const uint64_t MAXIMUM_MIXIN_V1 = 100;

        const uint64_t MINIMUM_MIXIN_V2 = 7;
        const uint64_t MAXIMUM_MIXIN_V2 = 7;

        const uint64_t MINIMUM_MIXIN_V3 = 3;
        const uint64_t MAXIMUM_MIXIN_V3 = 3;

        const uint64_t MINIMUM_MIXIN_V4 = 1;
        const uint64_t MAXIMUM_MIXIN_V4 = 5;

        /* The heights to activate the mixin limits at */
        const uint32_t MIXIN_LIMITS_V1_HEIGHT = 440000;
        const uint32_t MIXIN_LIMITS_V2_HEIGHT = 620000;
        const uint32_t MIXIN_LIMITS_V3_HEIGHT = 800000;
        const uint32_t MIXIN_LIMITS_V4_HEIGHT = 1250000;

        /* The mixin to use by default with zedwallet and turtle-service */
        /* DEFAULT_MIXIN_V0 is the mixin used before MIXIN_LIMITS_V1_HEIGHT is started */
        const uint64_t DEFAULT_MIXIN_V0 = 3;
        const uint64_t DEFAULT_MIXIN_V1 = MAXIMUM_MIXIN_V1;
        const uint64_t DEFAULT_MIXIN_V2 = MAXIMUM_MIXIN_V2;
        const uint64_t DEFAULT_MIXIN_V3 = MAXIMUM_MIXIN_V3;
        const uint64_t DEFAULT_MIXIN_V4 = MAXIMUM_MIXIN_V4;

        const uint64_t DEFAULT_DUST_THRESHOLD = UINT64_C(10);
        const uint64_t DEFAULT_DUST_THRESHOLD_V2 = UINT64_C(0);

        const uint32_t DUST_THRESHOLD_V2_HEIGHT = MIXIN_LIMITS_V2_HEIGHT;
        const uint32_t FUSION_DUST_THRESHOLD_HEIGHT_V2 = 800000;
        const uint64_t EXPECTED_NUMBER_OF_BLOCKS_PER_DAY = 24 * 60 * 60 / DIFFICULTY_TARGET;

        const size_t DIFFICULTY_WINDOW = 17;
        const size_t DIFFICULTY_WINDOW_V1 = 2880;
        const size_t DIFFICULTY_WINDOW_V2 = 2880;
        const uint64_t DIFFICULTY_WINDOW_V3 = 60;
        const uint64_t DIFFICULTY_BLOCKS_COUNT_V3 = DIFFICULTY_WINDOW_V3 + 1;

        const size_t DIFFICULTY_CUT = 0; // timestamps to cut after sorting
        const size_t DIFFICULTY_CUT_V1 = 60;
        const size_t DIFFICULTY_CUT_V2 = 60;
        const size_t DIFFICULTY_LAG = 0; // !!!
        const size_t DIFFICULTY_LAG_V1 = 15;
        const size_t DIFFICULTY_LAG_V2 = 15;
        static_assert(2 * DIFFICULTY_CUT <= DIFFICULTY_WINDOW - 2, "Bad DIFFICULTY_WINDOW or DIFFICULTY_CUT");

        const size_t MAX_BLOCK_SIZE_INITIAL = 100000;
        const uint64_t MAX_BLOCK_SIZE_GROWTH_SPEED_NUMERATOR = 100 * 1024;
        const uint64_t MAX_BLOCK_SIZE_GROWTH_SPEED_DENOMINATOR = 365 * 24 * 60 * 60 / DIFFICULTY_TARGET;
        const uint64_t MAX_EXTRA_SIZE = 2200;
        const uint64_t MAX_EXTRA_SIZE_V2 = 1024;
        const uint64_t MAX_EXTRA_SIZE_V2_HEIGHT = 1300000;
        const uint64_t MAX_EXTRA_SIZE_POOL = 2200; // Includes Hugin Messages in pool
        const uint64_t MAX_EXTRA_SIZE_BLOCK = 128; // Excludes Hugin Messages from blocks
        const uint64_t BLOCK_BLOB_SHUFFLE_CHECK_HEIGHT = 1750000;
        const uint64_t TRANSACTION_SIGNATURE_COUNT_VALIDATION_HEIGHT = 1750000;

        const uint64_t MEVACOIN_LOCKED_TX_ALLOWED_DELTA_BLOCKS = 1;
        const uint64_t MEVACOIN_LOCKED_TX_ALLOWED_DELTA_SECONDS = DIFFICULTY_TARGET * MEVACOIN_LOCKED_TX_ALLOWED_DELTA_BLOCKS;

        const uint64_t MEVACOIN_MEMPOOL_TX_LIVETIME = 60 * 60 * 24;                   // seconds, 24 hours
        const uint64_t MEVACOIN_MEMPOOL_TX_FROM_ALT_BLOCK_LIVETIME = 60 * 10;         // seconds, one week
        const uint64_t MEVACOIN_NUMBER_OF_PERIODS_TO_FORGET_TX_DELETED_FROM_POOL = 7; // MEVACOIN_NUMBER_OF_PERIODS_TO_FORGET_TX_DELETED_FROM_POOL * MEVACOIN_MEMPOOL_TX_LIVETIME = time to forget tx

        const size_t FUSION_TX_MAX_SIZE = MEVACOIN_BLOCK_GRANTED_FULL_REWARD_ZONE_CURRENT * 30 / 100;
        const size_t FUSION_TX_MIN_INPUT_COUNT = 12;
        const size_t FUSION_TX_MIN_IN_OUT_COUNT_RATIO = 4;

        const uint32_t UPGRADE_HEIGHT_V2 = 1;
        const uint32_t UPGRADE_HEIGHT_V3 = 2;
        const uint32_t UPGRADE_HEIGHT_V4 = 3; // Upgrade height for CN-Lite Variant 1 switch.
        const uint32_t UPGRADE_HEIGHT_V5 = 4; // Upgrade height for CN-Turtle Variant 2 switch.
        const uint32_t UPGRADE_HEIGHT_CURRENT = UPGRADE_HEIGHT_V5;

        const unsigned UPGRADE_VOTING_THRESHOLD = 90;                             // percent
        const uint32_t UPGRADE_VOTING_WINDOW = EXPECTED_NUMBER_OF_BLOCKS_PER_DAY; // blocks
        const uint32_t UPGRADE_WINDOW = EXPECTED_NUMBER_OF_BLOCKS_PER_DAY;        // blocks
        static_assert(0 < UPGRADE_VOTING_THRESHOLD && UPGRADE_VOTING_THRESHOLD <= 100, "Bad UPGRADE_VOTING_THRESHOLD");
        static_assert(UPGRADE_VOTING_WINDOW > 1, "Bad UPGRADE_VOTING_WINDOW");

        /* Block heights we are going to have hard forks at */
        const uint64_t FORK_HEIGHTS[] =
            {
                187000,  // 0
                350000,  // 1
                440000,  // 2
                620000,  // 3
                700000,  // 4
                800000,  // 5
                1000000, // 6
                1200000, // 7
                1300000, // 8
                1400000, // 9
                1600000, // 10
                1800000, // 11
                2200000, // 12
        };

        /* MAKE SURE TO UPDATE THIS VALUE WITH EVERY MAJOR RELEASE BEFORE A FORK */
        const uint64_t SOFTWARE_SUPPORTED_FORK_INDEX = 12;

        const uint64_t FORK_HEIGHTS_SIZE = sizeof(FORK_HEIGHTS) / sizeof(*FORK_HEIGHTS);

        /* The index in the FORK_HEIGHTS array that this version of the software will
           support. For example, if CURRENT_FORK_INDEX is 3, this version of the
           software will support the fork at 600,000 blocks.

           This will default to zero if the FORK_HEIGHTS array is empty, so you don't
           need to change it manually. */
        const uint8_t CURRENT_FORK_INDEX = FORK_HEIGHTS_SIZE == 0 ? 0 : SOFTWARE_SUPPORTED_FORK_INDEX;

        /* Make sure CURRENT_FORK_INDEX is a valid index, unless FORK_HEIGHTS is empty */
        static_assert(FORK_HEIGHTS_SIZE == 0 || CURRENT_FORK_INDEX < FORK_HEIGHTS_SIZE, "CURRENT_FORK_INDEX out of range of FORK_HEIGHTS!");

        const char MEVACOIN_BLOCKS_FILENAME[] = "blocks.bin";
        const char MEVACOIN_BLOCKINDEXES_FILENAME[] = "blockindexes.bin";
        const char MEVACOIN_POOLDATA_FILENAME[] = "poolstate.bin";
        const char P2P_NET_DATA_FILENAME[] = "p2pstate.bin";
        const char MINER_CONFIG_FILE_NAME[] = "miner_conf.json";
    } // parameters

    const char MEVACOIN_NAME[] = "mevacoin";

    const uint8_t TRANSACTION_VERSION_1 = 1;
    const uint8_t TRANSACTION_VERSION_2 = 2;
    const uint8_t CURRENT_TRANSACTION_VERSION = TRANSACTION_VERSION_1;

    const uint8_t BLOCK_MAJOR_VERSION_1 = 1;
    const uint8_t BLOCK_MAJOR_VERSION_2 = 2;
    const uint8_t BLOCK_MAJOR_VERSION_3 = 3;
    const uint8_t BLOCK_MAJOR_VERSION_4 = 4;
    const uint8_t BLOCK_MAJOR_VERSION_5 = 5;

    const uint8_t BLOCK_MINOR_VERSION_0 = 0;
    const uint8_t BLOCK_MINOR_VERSION_1 = 1;

    const size_t BLOCKS_IDS_SYNCHRONIZING_DEFAULT_COUNT = 10000; // by default, blocks ids count in synchronizing
    const uint64_t BLOCKS_SYNCHRONIZING_DEFAULT_COUNT = 100;     // by default, blocks count in blocks downloading
    const size_t COMMAND_RPC_GET_BLOCKS_FAST_MAX_COUNT = 1000;

#ifdef USE_TESTNET
    const int P2P_DEFAULT_PORT = 17078;
    const int RPC_DEFAULT_PORT = 17079;
#else
    const int P2P_DEFAULT_PORT = 17080;
    const int RPC_DEFAULT_PORT = 17081;
#endif

    const int SERVICE_DEFAULT_PORT = 8070;

    const size_t P2P_LOCAL_WHITE_PEERLIST_LIMIT = 1000;
    const size_t P2P_LOCAL_GRAY_PEERLIST_LIMIT = 5000;

    // P2P Network Configuration Section - This defines our current P2P network version
    // and the minimum version for communication between nodes
    const uint8_t P2P_CURRENT_VERSION = 5;
    const uint8_t P2P_MINIMUM_VERSION = 4;

    // This defines the minimum P2P version required for lite blocks propogation
    const uint8_t P2P_LITE_BLOCKS_PROPOGATION_VERSION = 4;

    // This defines the number of versions ahead we must see peers before we start displaying
    // warning messages that we need to upgrade our software.
    const uint8_t P2P_UPGRADE_WINDOW = 2;

    const size_t P2P_CONNECTION_MAX_WRITE_BUFFER_SIZE = 32 * 1024 * 1024; // 32 MB
    const uint32_t P2P_DEFAULT_CONNECTIONS_COUNT = 8;
    const size_t P2P_DEFAULT_WHITELIST_CONNECTIONS_PERCENT = 70;
    const uint32_t P2P_DEFAULT_HANDSHAKE_INTERVAL = 60;    // seconds
    const uint32_t P2P_DEFAULT_PACKET_MAX_SIZE = 50000000; // 50000000 bytes maximum packet size
    const uint32_t P2P_DEFAULT_PEERS_IN_HANDSHAKE = 250;
    const uint32_t P2P_DEFAULT_CONNECTION_TIMEOUT = 5000;      // 5 seconds
    const uint32_t P2P_DEFAULT_PING_CONNECTION_TIMEOUT = 2000; // 2 seconds
    const uint64_t P2P_DEFAULT_INVOKE_TIMEOUT = 60 * 2 * 1000; // 2 minutes
    const size_t P2P_DEFAULT_HANDSHAKE_INVOKE_TIMEOUT = 5000;  // 5 seconds
    const char P2P_STAT_TRUSTED_PUB_KEY[] = "";

    const uint64_t DATABASE_WRITE_BUFFER_MB_DEFAULT_SIZE = 256;
    const uint64_t DATABASE_READ_BUFFER_MB_DEFAULT_SIZE = 10;
    const uint32_t DATABASE_DEFAULT_MAX_OPEN_FILES = 100;
    const uint16_t DATABASE_DEFAULT_BACKGROUND_THREADS_COUNT = 2;

    const char LATEST_VERSION_URL[] = "https://github.com/mevacoin/mevacoin";
    const std::string LICENSE_URL = "https://github.com/mevacoin/mevacoin/blob/master/LICENSE";

#ifdef USE_TESTNET
    const static boost::uuids::uuid MEVACOIN_NETWORK =
        {
            {0x72, 0x50, 0x56, 0x3e, 0x03, 0x0d, 0x4d, 0x99, 0x8c, 0x01, 0x35, 0x28, 0x38, 0xbf, 0xd5, 0x6b}};

    const char *const SEED_NODES[] = {};
#else
    const static boost::uuids::uuid MEVACOIN_NETWORK =
        {
            {0x72, 0x50, 0x56, 0x3e, 0x03, 0x0d, 0x4d, 0x99, 0x8c, 0x01, 0x35, 0x28, 0x38, 0xbf, 0xd5, 0x6b}};

    const char *const SEED_NODES[] = {
        "195.231.65.38:17080", 
"195.231.76.77:17080"
        
    };
#endif

} // MevaCoin 
